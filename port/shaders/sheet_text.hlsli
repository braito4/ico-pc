// sheet_text.hlsli: the sheet texel of font_sheet_ps (font.hlsl).
// rd.h rd_create_texture_sheet says what a sheet texture is
// and what the shader makes of it; port/render/test/sheet_ref.c is the
// same arithmetic on the CPU (the tests' oracle).
//
// The constants are mirrored as ICO_SHEET_* in shader_consts.h, which C
// reads; the two must hold the same values (sheet_ref.c includes this file
// with ICO_SHEET_TEXT_C defined, which leaves the defines and the noise
// words alone, and static-asserts each pair).  Every step is integer
// arithmetic, so the GPU and the CPU agree exactly on the texels; only the
// bilinear weights are float.  The halo itself is the texture's bottom half,
// made on the CPU (rd.h rd_sheet_rim).
#ifndef ICO_SHEET_TEXT_HLSLI
#define ICO_SHEET_TEXT_HLSLI

#include "sheet_noise.hlsli"

// The levels the opacity and the halo-to-fill mix are quantised to.
#define SHEET_LEVELS 6
// The grain's spread, 64ths of a step (0.60), and the threshold (512ths of
// a step) with dither off: a half, rounding.
#define SHEET_GRAIN 38
#define SHEET_T_OFF 256

#ifndef ICO_SHEET_TEXT_C

// The 32 x 32 blue-noise tile (sheet_noise.hlsli's words, which rd_tex.c
// rd__sheet_noise_binding puts in a uniform buffer bound in every draw
// group at group 1, slot 4: shader_consts.h ICO_SHEET_NOISE_SLOT): texel
// (x, y) is byte x & 3 of word y * 8 + x / 4, 0..255, the word i component
// i & 3 of vector i / 4.  A uniform block, not an array in the shader: DXC
// makes a static array a per-invocation copy in SPIR-V (a Function
// variable stored on every invocation), which a mobile GPU may keep in
// scratch memory.
cbuffer SheetNoiseCB : register(b4, space1)
{
    uint4 g_sheetNoise[SHEET_NOISE_SIZE * SHEET_NOISE_SIZE / 16];
};

// The threshold (in 512ths of a step) at texel p of the texture: the
// tile's entry for p spread SHEET_GRAIN / 64 of a step round the half,
// 256 + (2 rank + 1 - 256) * SHEET_GRAIN / 64 rounded to nearest (the + 32;
// kept unsigned); dither 0: SHEET_T_OFF.
uint sheet_threshold(int2 p, uint dither)
{
    if (dither == 0u) {
        return SHEET_T_OFF;
    }
    const uint x = ((uint)p.x) & 31u, y = ((uint)p.y) & 31u;
    const uint i = y * 8u + (x >> 2);
    const uint rank = (g_sheetNoise[i >> 2][i & 3u] >> (8u * (x & 3u))) & 255u;
    return ((2u * rank + 1u) * (uint)SHEET_GRAIN + 256u * (64u - (uint)SHEET_GRAIN) + 32u) / 64u;
}

// v 0..255 quantised to SHEET_LEVELS levels against threshold th (512ths
// of a step), returned on the 0..255 scale (0, 51, 102, 153, 204, 255 for
// six levels).
uint sheet_quantise(uint v, uint th)
{
    const uint n = (uint)SHEET_LEVELS - 1u;
    const uint q = (v * n * 512u + th * 255u) / (255u * 512u);
    return (min(q, n) * 255u + n / 2u) / n;
}

// The sheet texel from its coverage c and halo r (both 0..255) under the
// style (the halo's weight 0..255 in 64ths, rimLevel, fillLevel, dither) at
// threshold th: x grey 0..255, y alpha in GS units (0x80 full).  The
// letter's own coverage always wins over the halo.
uint2 sheet_texel(uint c, uint r, uint4 style, uint th)
{
    const uint a = max(c, min(255u, (r * style.x + 32u) / 64u));
    const uint t = a != 0u ? (c * 255u + a / 2u) / a : 0u;
    const uint aq = sheet_quantise(a, th), tq = sheet_quantise(t, th);
    const uint grey = (style.y * (255u - tq) + style.z * tq + 127u) / 255u;
    return uint2(grey, (aq * 128u + 127u) / 255u);
}

#endif // ICO_SHEET_TEXT_C
#endif // ICO_SHEET_TEXT_HLSLI
