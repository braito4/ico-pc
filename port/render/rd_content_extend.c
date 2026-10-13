/* rd_content_extend.c: the content table's sheets, the stage overlays made
 * to cover the PS2's 4:3 picture that the native frame continues to its
 * edges.
 *
 * A few stage models are sheets of tiles held in front of the camera: their
 * tiles cover the PS2's 4:3 picture with some margin and no more, so on a
 * frame wider than 4:3 the sheet stops short of the sides.  They are
 * listed here by the bits of their stream (the vertex count, the layout and
 * xxh3_64 of the vertexCount * qwPerVertex quadwords the mesh is made from):
 * a model pack's replacement or any other geometry never matches by
 * accident.  rd_mesh.c (extendSheet, rd__sheet_tiles) appends whole tiles
 * around a listed sheet when the mesh is made: each tile of the sheet's
 * outer column is repeated `cols` times outward across, then each tile of
 * its outer row (the new ones included) `rows` times outward down, its UV
 * continued from the tile it repeats (the same tile one width further out
 * has its U one tile's U span further on, so the texture runs on without a
 * seam under REPEAT).  The appended triangles are part of the same batch:
 * every draw of the mesh draws them with its own state, matrices and UV
 * offset (the texture's scroll), and they pass or fail the region test as
 * any triangle of the draw (vu_common.hlsli vu_region_rule).
 *
 * The verb:
 *   RD_EXTEND_TILES  the tiles appended.  Listed only when the appended
 *              tiles lie wholly beside the 4:3 picture, so that the frame
 *              at 4:3 (Original 1x and Enhanced 2x) is the same, byte for
 *              byte, with the entry off and on
 *              (tools/content_table_check.py decide), and a wide frame's
 *              sides then show the sheet as its middle does.
 * A new entry comes from a report: find the mesh (rd_replay_tool --list and
 * --mesh on the player's dump; its draws' GS extent with --vu-ref L:I),
 * hash its stream, size cols and rows from that extent, and run
 * tools/content_table_check.py decide on dumps that draw it.
 */
#include "rd_internal.h"

/* Each row authored from a dump, decided on it (the 4:3 picture never shows
   the tiles) */
const RdContentExtend rd__content_extend[] = {
    /* the deja vision (stage 40, frame v2227 at 48:9): the stage's grain
     * veil, mesh "sandstorm", four quadrant sheets of four 128 x 112 tiles
     * (model x -256..256, y -224..224), each drawn twice a frame (two
     * layers, ALPHA 0 FIX 9 and ALPHA 1 FIX 4, the noise texture under
     * REPEAT, about 7 repeats a tile across and 4 down).  Their GS extent
     * (rd_replay_tool --vu-ref, every vertex at one depth: the sheets face
     * the camera): X 2048 -+ 702.3 (the second layer -+ 690), Y 2048 -+
     * 716.9, so one tile is 351 GS pixels across and 358 down.  The widest
     * frame, 60:9, is 2560 GS pixels across (2048 -+ 1280): 578 more a side,
     * two tiles; three (2048 -+ 1755, the vertices still inside the GS
     * window 0..4094, so the tiles take the region test as the sheet does)
     * leave room for a camera that shows the sheets smaller.  Down, the
     * sheets already cover the 448 lines 3.2 times; one row (2048 -+ 1075)
     * the same.  At 4:3 (2048 -+ 256) every appended tile lies off the
     * frame (content_table_check.py decide on v2227 at 4:3: the same
     * picture, Original 1x and Enhanced 2x): TILES */
    {"sandstorm: the deja veil, the upper right quadrant (x 0..256, y -224..0)", RD_EXTEND_TILES,
     16, 3, 0x876eda2730a28055ull, 3, 1},
    {"sandstorm: the deja veil, the lower right quadrant (x 0..256, y 0..224)", RD_EXTEND_TILES, 16,
     3, 0x186ee847d2a7b94dull, 3, 1},
    {"sandstorm: the deja veil, the lower left quadrant (x -256..0, y 0..224)", RD_EXTEND_TILES, 16,
     3, 0xbb3c46c1a9b86f1cull, 3, 1},
    {"sandstorm: the deja veil, the upper left quadrant (x -256..0, y -224..0)", RD_EXTEND_TILES,
     16, 3, 0xde55e9f2fb633792ull, 3, 1},
};

#define RD_CONTENT_EXTEND_COUNT (sizeof(rd__content_extend) / sizeof(rd__content_extend[0]))

_Static_assert(RD_CONTENT_EXTEND_COUNT <= RD_CONTENT_EXTEND_MAX, "the sheets table's size");

const int rd__content_extend_count = (int)RD_CONTENT_EXTEND_COUNT;
