// font.hlsl: font_ps, screen prims that sample an R8 coverage texture
// (rd_create_texture_r8: the port's font atlas pages, port/ui/font.c).
// rd__plan_screen_draw selects it (RD_FS_FONT) for the bound texture's format
// behind sprite_ui_vs or sprite_world_vs, with sprite_ps's bind groups: the
// atlas at t1 with its sampler, the DATE snapshot at t2.
//
// The texel is the alpha an RGBA8 atlas would hold (coverage in GS units,
// 0x80 = full) and stands for a white texel with that alpha:
// read back as sprite_ps reads a texel, then sprite_ps's texture function,
// TCC, alpha test, DATE and dual-source output, so the bytes are those of
// the RGBA8 path, filtered or not (the sampler filters the same UNORM8
// values).
//
// font_sheet_ps: screen and overlay prims that sample
// a sheet texture (rd_create_texture_sheet, RD_FS_FONT_SHEET): R8 at the
// same t1, the coverage 0..255 in its top half and the halo (rd_sheet_rim)
// in its bottom half, the style in g_param (the halo's weight times its gain
// in 64ths, rimLevel, fillLevel, dither, as rd_replay.c sheetParam writes
// them).  rd.h says what it draws;
// sheet_text.hlsli holds the texel arithmetic.
#include "common.hlsli"
#include "sheet_text.hlsli"

// The vertex shader's output: must match sprite.hlsl's SpriteVSOut, which
// sprite_ui_vs and sprite_world_vs write.
struct SpriteVSOut
{
    float4 pos : SV_Position;
    VK_LOC(0) float4 col : COLOR0; // 0..255, interpolated
    VK_LOC(1) float2 uv : TEXCOORD0; // normalised
};

Texture2D<float4> g_atlas : register(t1, space2);
SamplerState g_sampler : register(s1, space2);
Texture2D<float> g_dateSnap : register(t2, space2);

GS_PS_ATTR GS_PS_OUT font_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint a = (uint)floor(g_atlas.Sample(g_sampler, i.uv).r * 255.0 + 0.5);
        col = gs_texture_function(uint4(255u, 255u, 255u, a), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    GS_RETURN(col, i.pos);
}

// The bytes of the 2x2 texels at q (x), q + (1, 0) (y), q + (0, 1) (z) and
// q + (1, 1) (w) of one half of t1 (rows off .. off + hh - 1: the
// coverage at off 0, the halo at off hh), in one gather (the sample point is
// the corner the four share, half a texel from any other footprint); 0
// outside the half, whatever the sampler's address mode.
uint4 sheet_half4(int2 q, int2 size, int hh, int off)
{
    const float4 v =
        g_atlas.GatherRed(g_sampler, (float2(q + int2(0, off)) + 1.0) / float2(size));
    // Gather's order: x (0, 1), y (1, 1), z (1, 0), w (0, 0)
    uint4 c = uint4(floor(float4(v.w, v.z, v.x, v.y) * 255.0 + 0.5));
    const bool in0x = q.x >= 0 && q.x < size.x, in1x = q.x + 1 >= 0 && q.x + 1 < size.x;
    const bool in0y = q.y >= 0 && q.y < hh, in1y = q.y + 1 >= 0 && q.y + 1 < hh;
    c.x = in0x && in0y ? c.x : 0u;
    c.y = in1x && in0y ? c.y : 0u;
    c.z = in0x && in1y ? c.z : 0u;
    c.w = in1x && in1y ? c.w : 0u;
    return c;
}

GS_PS_ATTR GS_PS_OUT font_sheet_ps(SpriteVSOut i)
{
    if ((g_mode.x & DF_DATE) != 0u) {
        if (gs_date_discard(g_mode.x, g_dateSnap.Load(int3(int2(i.pos.xy), 0)))) {
            discard;
        }
    }
    uint4 col = uint4(floor(i.col + 0.5));
    if ((g_mode.x & DF_TEXTURED) != 0u) {
        uint tw, th;
        g_atlas.GetDimensions(tw, th);
        const int2 size = int2((int)tw, (int)th);
        const int hh = size.y / 2;
        const uint4 style = uint4(g_param + 0.5);
        const uint dither = style.w & 1u;
        // the four texels around the sample (a bilinear read with the texel
        // centres at +0.5; the draw addresses the top half), each made from
        // its coverage and its halo: two gathers
        const float2 pos = i.uv * float2(size) - 0.5;
        const float2 base = floor(pos);
        const float2 f = pos - base;
        const int2 p0 = int2(base);
        const uint4 c = sheet_half4(p0, size, hh, 0);
        const uint4 r = sheet_half4(p0, size, hh, hh);
        const uint2 t00 = sheet_texel(c.x, r.x, style, sheet_threshold(p0, dither));
        const uint2 t10 = sheet_texel(c.y, r.y, style, sheet_threshold(p0 + int2(1, 0), dither));
        const uint2 t01 = sheet_texel(c.z, r.z, style, sheet_threshold(p0 + int2(0, 1), dither));
        const uint2 t11 = sheet_texel(c.w, r.w, style, sheet_threshold(p0 + int2(1, 1), dither));
        const float2 m = lerp(lerp(float2(t00), float2(t10), f.x),
                              lerp(float2(t01), float2(t11), f.x), f.y);
        const uint2 gm = uint2(floor(m + 0.5));
        col = gs_texture_function(uint4(gm.x, gm.x, gm.x, gm.y), col, g_mode.x);
    }
    if (gs_alpha_discard(g_mode.z, g_mode.w, col.a)) {
        discard;
    }
    GS_RETURN(col, i.pos);
}
