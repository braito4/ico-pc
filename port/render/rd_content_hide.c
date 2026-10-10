/* rd_content_hide.c: the content table, the stage geometry made for the
 * PS2's 4:3 picture that the native frame must not show.
 *
 * The renderer draws the native frustum at every aspect (vu_common.hlsli
 * vu_triangle_out): outside the Original preset a triangle that fails a VU
 * program's region test is drawn, clipped by the GPU, instead of dropped.
 * A few stage faces relied on the PS2's 4:3 picture and its drops: closing
 * planes that seal a model where the 4:3 camera never looks, cards a camera
 * passes through.  They are listed here as data, by the stream bits of
 * their corners (prelit and lit layouts), and rd_mesh.c markContentHidden
 * marks a triangle whose three vertices are black (vertex colour 0, 0, 0,
 * which MODULATE turns black whatever the texel) and are corners of one
 * listed entry.  Black alone does not tell such a face: the stages use it
 * for faces in full shade too (the bench's statue pedestals, st17a; the
 * pool, st09a), which every picture shows.  A model pack's replacement or
 * any other geometry never matches by accident.
 *
 * The verbs (shader_consts.h ICO_VU_INDEX_HIDE, ICO_VU_INDEX_HIDE43):
 *   RD_HIDE    drawn nowhere: a replay shows the triangles never reach the
 *              4:3 picture (tools/content_table_check.py: the dumps at 4:3
 *              with the entry hidden are the same, byte for byte, as
 *              without the table)
 *   RD_HIDE43  drawn only inside the 4:3 columns of a frame-wide target
 *              (rd_replay.c doVu splits the draw in three: the columns,
 *              and the two sides with ICO_VU_DROP_HIDDEN), and there as the
 *              PS2 draws it (a triangle failing the region test dropped in
 *              every preset, vu_region_drops)
 * At 4:3 an entry changes nothing in the Original preset: HIDE43 draws the
 * PS2's triangles in one draw, and HIDE is listed only when the check
 * proves the same picture.  A new entry comes from a report: find the
 * mesh (rd_replay_tool --list and --mesh on the player's dump), list its
 * black corners here, and run tools/content_table_check.py on the dump to
 * choose the verb.
 *
 * Not listed, and why (tools/content_table_check.py reports them on the
 * opening's dumps, v722, v934 and v1251, at 16:9, 32:9 and 48:9):
 *   st26a_p1    the cliff of the opening, which the camera passes through
 *   st26a_near  the forest's tree card after the Sony sign (issue 59)
 *   The PS2 drops their triangles that fail the 4:3 window test (a vertex
 *   past the window, or behind the eye as the camera passes through), and
 *   v0.5.3 kept that drop inside the 4:3 picture.  The plan's expectation
 *   (v0.6.0 D3) is that on the native frame these triangles are inside
 *   the window or clipped by the GPU at the native frustum, and that what
 *   they draw then shows no rock or trunk the 4:3 picture did not show.
 *   That is unverified here: tools/content_table_check.py measures the
 *   pixels each mesh adds when clipped (Enhanced against the Original
 *   rule, at one scale) and where they land, and an entry is added (HIDE
 *   or HIDE43, by the same check) if they show what the original did not.
 *   st02a wf_smoke1  the waterfall's mist billboards (issue 57): clipped,
 *   the mist is drawn inside the picture too; listed only if the patched
 *   waterfall dump's report shows that worse than the 4:3 picture.
 */
#include "rd_internal.h"

const RdContentHide rd__content_hide[] = {
    /* the long walkway (issue 29's frame v31438 at 16:9): a black box 846
     * units wide through the arch at the walkway's end showed as a flat
     * grey block (the fog over black) beside the 4:3 picture; its top is a
     * four-vertex mesh of its own, its two sides the last eight vertices
     * of the next one, the textured stonework drawn behind it.  The 4:3
     * picture shows its faces: HIDE43 */
    {"st08a_p4: the box through the arch at the walkway's end",
     RD_HIDE43,
     7,
     {{0xc518a521u, 0xc51a5cc4u, 0xc431a218u},   /* -2442.32 -2469.80  -710.53 */
      {0xc518a521u, 0xc5696b2bu, 0xc431a218u},   /* -2442.32 -3734.70  -710.53 */
      {0xc518a521u, 0xc51a5cc4u, 0xc4a092d2u},   /* -2442.32 -2469.80 -1284.59 */
      {0xc518a521u, 0xc5696b2bu, 0xc4a092d2u},   /* -2442.32 -3734.70 -1284.59 */
      {0xc4c78446u, 0xc51a5cc4u, 0xc42abb35u},   /* -1596.13 -2469.80  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc42abb35u},   /* -1596.13 -3734.70  -682.93 */
      {0xc4c78446u, 0xc5696b2bu, 0xc45c93adu}}}, /* -1596.13 -3734.70  -882.31 */
};

#define RD_CONTENT_HIDE_COUNT (sizeof(rd__content_hide) / sizeof(rd__content_hide[0]))

_Static_assert(RD_CONTENT_HIDE_COUNT <= RD_CONTENT_HIDE_MAX, "the content table's size");

const int rd__content_hide_count = (int)RD_CONTENT_HIDE_COUNT;
