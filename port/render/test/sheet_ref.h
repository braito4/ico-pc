/* sheet_ref.h: font_sheet_ps and rd_sheet_rim on the CPU, the oracle of
 * the sheet-text tests (rd_tex_test testSheetRim, rd_pixel_test
 * testSheetText, port/ui/test's menu comparisons).  rd.h
 * rd_create_texture_sheet says what the two make; this is the same
 * arithmetic with the same constants (shader_consts.h ICO_SHEET_*, checked
 * against sheet_text.hlsli at compile time), written apart from both.
 *
 * cov is a w x h coverage strip (0..255 a texel, rows packed), the top half
 * of a sheet texture; texels outside it have coverage 0.  style may be
 * null: rd_create_texture_sheet's default {1, 0, 255, 1}.  style->scale is
 * the strip texels a sheet texel (0 is 1): the halo's taps are the 1x ones
 * interpolated at 1 / scale of a sheet texel.
 *
 * sheetref_halo_sum  the halo's weighted sum at texel (x, y), x and y any
 *                  integers: sum of c(x + i, y + j) KXs[|i|] KYs[|j|] over
 *                  the taps; *den (if not null) the sum of the taps,
 *                  s^4 * 11264 * 8192 (a plane of coverage c sums to c * den)
 * sheetref_rim     the halo rd_sheet_rim writes for texel (x, y):
 *                  min(255, (sum * ICO_SHEET_GAIN + den * 32) / (den * 64))
 * sheetref_noise   the blue-noise tile's entry (0..255) at texel (x, y)
 * sheetref_weight  the halo's weight the shader reads (DrawCB.param.x): 0
 *                  without the rim, else the rim weight (1..63, else 64)
 *                  times the gain (0 is 64) / 64, rounded
 * sheetref_texel   the sheet texel (x, y), x and y any integers: grey
 *                  0..255 and alpha in GS units (0x80 full), from the
 *                  coverage and sheetref_rim (0 outside the strip)
 * sheetref_texel_from  the same from texel (x, y)'s coverage c and halo r
 *                  (0..255 each) given: the shader's arithmetic alone
 * sheetref_sample  the texel value font_sheet_ps passes on at normalised
 *                  (u, v) of the w x 2h texture (the interpolated UV of a
 *                  pixel): the four sheet texels around u * w - 0.5,
 *                  v * 2h - 0.5 blended bilinearly in float and rounded, as
 *                  rgba (r = g = b = the grey, a the alpha), before the
 *                  texture function */
#ifndef PORT_RENDER_TEST_SHEET_REF_H
#define PORT_RENDER_TEST_SHEET_REF_H

#include <stdint.h>
#include "rd.h"

uint64_t sheetref_halo_sum(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                           uint32_t scale, uint64_t *den);
uint8_t sheetref_rim(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                     uint32_t scale);
uint32_t sheetref_noise(int32_t x, int32_t y);
uint32_t sheetref_weight(const RdSheetStyle *style);
void sheetref_texel(const uint8_t *cov, uint32_t w, uint32_t h, int32_t x, int32_t y,
                    const RdSheetStyle *style, uint8_t *outGrey, uint8_t *outAlpha);
void sheetref_texel_from(uint32_t c, uint32_t r, int32_t x, int32_t y, const RdSheetStyle *style,
                         uint8_t *outGrey, uint8_t *outAlpha);
void sheetref_sample(const uint8_t *cov, uint32_t w, uint32_t h, float u, float v,
                     const RdSheetStyle *style, uint8_t rgba[4]);

#endif
