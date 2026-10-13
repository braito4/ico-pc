/* sheet_ref.c: font_sheet_ps (port/shaders/font.hlsl) and rd_sheet_rim
 * (port/render/rd_tex.c) on the CPU; see sheet_ref.h.  Written apart from
 * the shader and from rd_tex.c, from rd.h's description, with the constants
 * of shader_consts.h, so a test that compares them compares two
 * implementations (the halo here is the direct 2D sum, rd_tex.c's the
 * separable one; integer sums, so the two agree exactly). */
#include "sheet_ref.h"
#include <math.h>
#include "shader_consts.h"

/* the HLSL twins (sheet_text.hlsli's defines and the noise words only): the
   same-value rule of shader_consts.h, checked here */
#define ICO_SHEET_TEXT_C 1
#include "sheet_text.hlsli"

_Static_assert(SHEET_LEVELS == ICO_SHEET_LEVELS, "sheet_text.hlsli SHEET_LEVELS");
_Static_assert(SHEET_GRAIN == ICO_SHEET_GRAIN, "sheet_text.hlsli SHEET_GRAIN");
_Static_assert(SHEET_T_OFF == ICO_SHEET_T_OFF, "sheet_text.hlsli SHEET_T_OFF");
_Static_assert(SHEET_NOISE_SIZE == 32, "sheet_noise.hlsli is a 32 x 32 tile");
_Static_assert(ICO_SHEET_LEVELS >= 2 && ICO_SHEET_LEVELS <= 256, "sheet levels");
_Static_assert(ICO_SHEET_GRAIN >= 0 && ICO_SHEET_GRAIN <= 64, "the grain is at most a step");
_Static_assert(ICO_SHEET_T_OFF == 256, "dither off rounds");
_Static_assert(ICO_SHEET_GAIN > 0 && ICO_SHEET_GAIN < 1024, "the halo's gain");
_Static_assert(ICO_SHEET_SCALE_MAX >= 1 && ICO_SHEET_SCALE_MAX <= 4,
               "a 4x halo's sums fit (x: uint32, y: uint64)");
/* the reach the ramps were made for: the taps' tables below have RX + 1 and
   RY + 1 entries, a linear ramp to 0 one texel past the reach */
_Static_assert(ICO_SHEET_RX == 10 && ICO_SHEET_RY == 7, "the halo's reach");
_Static_assert(ICO_SHEET_KX_0 == 1024 && ICO_SHEET_KY_0 == 1024, "the taps are 1024 at 0");
_Static_assert(ICO_SHEET_KX_0 +
                       2 * (ICO_SHEET_KX_1 + ICO_SHEET_KX_2 + ICO_SHEET_KX_3 + ICO_SHEET_KX_4 +
                            ICO_SHEET_KX_5 + ICO_SHEET_KX_6 + ICO_SHEET_KX_7 + ICO_SHEET_KX_8 +
                            ICO_SHEET_KX_9 + ICO_SHEET_KX_10) ==
                   11264,
               "the taps across sum to 11 x 1024");
_Static_assert(ICO_SHEET_KY_0 +
                       2 * (ICO_SHEET_KY_1 + ICO_SHEET_KY_2 + ICO_SHEET_KY_3 + ICO_SHEET_KY_4 +
                            ICO_SHEET_KY_5 + ICO_SHEET_KY_6 + ICO_SHEET_KY_7) ==
                   8192,
               "the taps down sum to 8 x 1024");
_Static_assert(ICO_SHEET_KX_10 > 0 && ICO_SHEET_KX_10 < ICO_SHEET_KX_9 && ICO_SHEET_KY_7 > 0 &&
                   ICO_SHEET_KY_7 < ICO_SHEET_KY_6,
               "the ramps fall to their last tap and reach 0 one texel past it");

/* the taps across (|dx| 0..RX) and down (|dy| 0..RY), and 0 one past each */
static const uint32_t kKx[ICO_SHEET_RX + 2] = {
    ICO_SHEET_KX_0, ICO_SHEET_KX_1, ICO_SHEET_KX_2, ICO_SHEET_KX_3, ICO_SHEET_KX_4,  ICO_SHEET_KX_5,
    ICO_SHEET_KX_6, ICO_SHEET_KX_7, ICO_SHEET_KX_8, ICO_SHEET_KX_9, ICO_SHEET_KX_10, 0};
static const uint32_t kKy[ICO_SHEET_RY + 2] = {ICO_SHEET_KY_0, ICO_SHEET_KY_1, ICO_SHEET_KY_2,
                                               ICO_SHEET_KY_3, ICO_SHEET_KY_4, ICO_SHEET_KY_5,
                                               ICO_SHEET_KY_6, ICO_SHEET_KY_7, 0};

/* the noise tile, the shader's words */
static const uint32_t kNoise[SHEET_NOISE_SIZE * SHEET_NOISE_SIZE / 4] = {SHEET_NOISE_WORDS};

static uint32_t covAt(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y)
{
    if (x < 0 || y < 0 || (uint32_t)x >= w || (uint32_t)y >= h) {
        return 0;
    }
    return cov[(size_t)y * w + (uint32_t)x];
}

static uint32_t clampScale(uint32_t s)
{
    return s > ICO_SHEET_SCALE_MAX ? ICO_SHEET_SCALE_MAX : s ? s : 1u;
}

/* the tap at distance d (strip texels) of a ramp k with n + 1 entries
   (k[n + 1] = 0) at s strip texels a sheet texel: k linearly interpolated
   at d / s, in 1 / (1024 s) */
static uint32_t tapAt(const uint32_t *k, int32_t n, int32_t d, int32_t s)
{
    d = d < 0 ? -d : d;
    const int32_t q = d / s, m = d % s;
    if (q > n) {
        return 0;
    }
    return k[q] * (uint32_t)(s - m) + k[q + 1] * (uint32_t)m;
}

uint64_t sheetref_halo_sum(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           uint32_t scale, uint64_t *den)
{
    const int32_t s = (int32_t)clampScale(scale);
    const int32_t rx = (ICO_SHEET_RX + 1) * s - 1, ry = (ICO_SHEET_RY + 1) * s - 1;
    uint64_t sum = 0;
    for (int32_t j = -ry; j <= ry; j++) {
        const uint64_t ky = tapAt(kKy, ICO_SHEET_RY, j, s);
        for (int32_t i = -rx; i <= rx; i++) {
            sum += (uint64_t)covAt(cov, w, h, x + i, y + j) * tapAt(kKx, ICO_SHEET_RX, i, s) * ky;
        }
    }
    if (den) {
        *den = (uint64_t)11264u * 8192u * (uint64_t)(s * s) * (uint64_t)(s * s);
    }
    return sum;
}

uint8_t sheetref_rim(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                     uint32_t scale)
{
    uint64_t den;
    const uint64_t sum = sheetref_halo_sum(cov, w, h, x, y, scale, &den);
    const uint64_t r = (sum * ICO_SHEET_GAIN + den * 32u) / (den * 64u);
    return (uint8_t)(r > 255u ? 255u : r);
}

uint32_t sheetref_noise(int32_t x, int32_t y)
{
    const uint32_t xx = (uint32_t)x & 31u, yy = (uint32_t)y & 31u;
    return (kNoise[yy * 8u + xx / 4u] >> (8u * (xx & 3u))) & 255u;
}

uint32_t sheetref_weight(const RdSheetStyle *s)
{
    if (!s->rimOn) {
        return 0;
    }
    const uint32_t wt = s->rimWeight && s->rimWeight < 64 ? s->rimWeight : 64u;
    const uint32_t gain = s->rimGain ? s->rimGain : 64u;
    return (wt * gain + 32u) / 64u;
}

/* v 0..255 to one of ICO_SHEET_LEVELS levels: the step below v plus th
   512ths of a step, on the 0..255 scale */
static uint32_t quantise(uint32_t v, uint32_t th512)
{
    const uint32_t n = ICO_SHEET_LEVELS - 1;
    uint32_t q = (v * n * 512u + th512 * 255u) / (255u * 512u);
    q = q > n ? n : q;
    return (q * 255u + n / 2u) / n;
}

/* the threshold (512ths) of texel (x, y): the noise entry's, or a half */
static uint32_t thresholdAt(const RdSheetStyle *s, int32_t x, int32_t y)
{
    if (!s->dither) {
        return ICO_SHEET_T_OFF;
    }
    /* 256 + (2 rank + 1 - 256) * GRAIN / 64, rounded to nearest (+ 32,
       then down) */
    const int32_t r = (int32_t)sheetref_noise(x, y);
    const int32_t num = (2 * r + 1 - 256) * ICO_SHEET_GRAIN + 32;
    const int32_t fl = num >= 0 ? num / 64 : -((-num + 63) / 64);
    return (uint32_t)(256 + fl);
}

void sheetref_texel_from(uint32_t c, uint32_t r, int32_t x, int32_t y, const RdSheetStyle *style,
                         uint8_t *outGrey, uint8_t *outAlpha)
{
    static const RdSheetStyle kDefault = {1, 0, 0xFF, 1, 0, 1, 0};
    const RdSheetStyle *s = style ? style : &kDefault;
    const uint32_t hw = (r * sheetref_weight(s) + 32u) / 64u;
    const uint32_t rw = hw > 255u ? 255u : hw;
    const uint32_t a = c > rw ? c : rw;
    const uint32_t t = a ? (c * 255u + a / 2u) / a : 0u;
    const uint32_t th = thresholdAt(s, x, y);
    const uint32_t aq = quantise(a, th), tq = quantise(t, th);
    *outGrey =
        (uint8_t)(((uint32_t)s->rimLevel * (255u - tq) + (uint32_t)s->fillLevel * tq + 127u) /
                  255u);
    *outAlpha = (uint8_t)((aq * 128u + 127u) / 255u);
}

void sheetref_texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha)
{
    const int inside = x >= 0 && y >= 0 && (uint32_t)x < w && (uint32_t)y < h;
    const uint32_t c = covAt(cov, w, h, x, y);
    const uint32_t r = inside ? sheetref_rim(cov, w, h, x, y, style ? style->scale : 1u) : 0u;
    sheetref_texel_from(c, r, x, y, style, outGrey, outAlpha);
}

void sheetref_sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4])
{
    const float px = u * (float)w - 0.5f, py = v * (float)(2u * h) - 0.5f;
    const float bx = floorf(px), by = floorf(py);
    const float fx = px - bx, fy = py - by;
    const int32_t x0 = (int32_t)bx, y0 = (int32_t)by;
    float g[2][2], al[2][2];
    for (int ty = 0; ty < 2; ty++) {
        for (int tx = 0; tx < 2; tx++) {
            uint8_t gr, a;
            sheetref_texel(cov, w, h, x0 + tx, y0 + ty, style, &gr, &a);
            g[ty][tx] = (float)gr;
            al[ty][tx] = (float)a;
        }
    }
    const float g0 = g[0][0] + (g[0][1] - g[0][0]) * fx, g1 = g[1][0] + (g[1][1] - g[1][0]) * fx;
    const float a0 = al[0][0] + (al[0][1] - al[0][0]) * fx,
                a1 = al[1][0] + (al[1][1] - al[1][0]) * fx;
    const uint8_t grey = (uint8_t)floorf(g0 + (g1 - g0) * fy + 0.5f);
    rgba[0] = rgba[1] = rgba[2] = grey;
    rgba[3] = (uint8_t)floorf(a0 + (a1 - a0) * fy + 0.5f);
}
