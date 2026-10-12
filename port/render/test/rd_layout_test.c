/* rd_layout_test.c: the layout frame.
 *
 * GifPacket.c, DisplayList.c and DmaPacket.c compiled as the window build
 * compiles them (ICO_HOST, ICO_RD) and fed the gif_* call sequence that
 * common/src/layout_texture.c's lt_draw_primary_sprite and display_texture
 * produce for one synthetic layout item, after the TEX0 packet
 * Texture.c's tex_setTexReg writes for the item's texture (tex_TransTexture
 * on a texture already in VRAM).  The calls are copied from those functions
 * with made-up layout values; no disc data.
 *
 * Checks, on the recording (no device needed):
 *   - list 11 holds, in order, the state of each packet (TEST 0x30000, Z
 *     write off, PABE 0 and ALPHA 0x44 from gif_SetAlpha(1, 7, 0)) and the
 *     sprites, UI-tagged, with the corners the GS would get;
 *   - the untextured sprite has the texture off, the textured one the
 *     texture-seam placeholder bound, TEX1 96 (linear/linear), FST UVs;
 *   - the glow sprite's gif_SetAlpha(1, 5, 0) is ALPHA 0x48;
 *   - nothing was written to a register the decoder does not decode;
 *   - the full-frame decision (v0.6.0): the primary sprite and an item whose
 *     grid rect spans 0 .. 640 (lt_rect_spans_grid: the pause and End Game
 *     bars) are drawn inside gif_FullFrameBegin / End, RD_SPACE_FULLFRAME,
 *     x scaled by the native frame's width / 640 (688 at 16:9: the item
 *     covers GS 2048 -+ 344); an item that does not span (a menu row) stays
 *     UI, 512 / 640 of the grid about the centre, whatever the frame's width;
 *   - a full-frame item's sparkle points (gif_PointOffset in the item's
 *     full-frame scope) are scaled by the native frame's width / 640 and
 *     full-frame-tagged, reaching past the 4:3 box as the item does;
 *   - the boot signs' backdrop (common/src/kanban.c, compiled in:
 *     kanbanExec on a layout with no items): the 640 x 224 grid sprite is
 *     full-frame, covering GS 2048 -+ 1024 at W = 2048 (48:9) in the
 *     layout's colour, and the UI box's numbers (2048 -+ 256) at 512;
 *   - a row sprite under gif_HostDrawKey carries the key, blends half
 *     way between two frames (rd__interp_frame), and the sprite after the
 *     key ends is unkeyed and the current frame's.
 * Then on a Vulkan device (exit 77 without one; lavapipe in the container),
 * the same frame replayed: a pixel inside the untextured sprite has its
 * colour, a pixel outside it the clear colour.
 *
 * Exit 0, 1 on a mismatch, 77 when there is no device (after the recording
 * checks passed).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "vk/rhi_vk.h"
/* the game's side */
#include "typedef.h"
#include "DisplayList.h"
#include "DmaPacket.h"
#include "GifHost.h"
#include "GifPacket.h"
#include "layout_texture.h"
#include "kanban.h"
#include "ico_frame.h"
#include "main.h"
#include "charFileManager.h"
#include "Texture.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* ------------------------------------------- what the three files import */
int ScreenWidth = 512, ScreenHeight = 512;
float center_X = 2048.0f, center_Y = 2048.0f;
int screenOffsetX, screenOffsetY;
int fbKeep;
int kanbanBootEnd;
int ico_stage_change_to;
int fadeStatus;
float fadeSpeed;
unsigned char fadeColor[4];
void *ios_partition_common;
void *dmaVif;

void *iosMallocDebug(void *part, int size, const char *file, int line)
{
    (void)part;
    (void)file;
    (void)line;
    return calloc(1, (size_t)size);
}

void iosFree(void *p)
{
    free(p);
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assert(const char *file, int line)
{
    printf("debug_assert %s:%d\n", file, line);
    abort();
}

void ico_assert(const char *file, int line, const char *e)
{
    printf("assert %s:%d %s\n", file, line, e);
    abort();
}

void mc_Reset(void) {}

/* port/math's matrix stack links against the game's sine table */
float GetTableSin(short angle)
{
    (void)angle;
    return 0.0f;
}

float GetTableCos(short angle)
{
    (void)angle;
    return 1.0f;
}

void FlushCache(int op)
{
    (void)op;
}

void sceDmaSend(void *ch, void *addr)
{
    (void)ch;
    (void)addr;
}

/* -------------------------------------------- what kanban.c imports */
LtProperty texProperty[1];
LtProp texLayout[4];
TexRec texFile[1];
const StgPre stageData[1];
int systemStatus[12];
PadState pad[16];

TexData *tex_GetTextureData(int idx)
{
    (void)idx;
    return NULL;
}

int tex_GetTextureNo(const char *name)
{
    (void)name;
    return 0;
}

void tex_SetSamplingType(TexData *tex, int mag, int min)
{
    (void)tex;
    (void)mag;
    (void)min;
}

int tex_TransTexture(int no, int pri)
{
    (void)no;
    (void)pri;
    return 0;
}

int lt_ext_is_text_row(const LtProperty *e)
{
    (void)e;
    return 0;
}

void lt_ext_draw_text_row(const LtProperty *e, const int box[4], const int uv[4],
                          const unsigned char col[4], int prim)
{
    (void)e;
    (void)box;
    (void)uv;
    (void)col;
    (void)prim;
}

/* ------------------------------------------------------ the layout calls */

/* layout_texture.c's SprRect / SprCol are GifRect / GifColor */
static const GifRect kPrimaryRect = {-160 * 16, -56 * 16, 320 * 16, 112 * 16};
static const GifColor kPrimaryCol = {200, 100, 50, 0x80};

/* tex_setTexReg's packet for a direct 256 x 256 PSMCT32 texture at TBP
   0x1A00, as Texture.c writes it: raw A+D pairs in the open packet */
#define TEST_TBP 0x1A00

static void texSetTexReg(int pri)
{
    gif_StartPacketPri(pri);
    *PacketBufferStruct.ptr.d++ = (unsigned long long)TEST_TBP | (4ull << 14) | (0ull << 20) |
                                  (8ull << 26) | (8ull << 30) | (1ull << 34);
    *PacketBufferStruct.ptr.d++ = 6; /* TEX0_1 */
    gif_EndPacket();
}

static void ltDrawPrimarySprite(void)
{
    GifRect r = kPrimaryRect;
    GifColor col = kPrimaryCol;

    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 7, 0);
    gif_FullFrameBegin(); /* lt_draw_primary_sprite: the backdrop is a full-frame item */
    gif_SpriteSensitive(&r, 0xFFFFFFFF, (void *)0, &col, 1);
    gif_FullFrameEnd();
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_EndPacket();
}

/* display_texture's masked == 0 branch for an item at dispX 100, dispY 40,
   tex 0,0 128x64, display size from the texture, selected with the glow on */
static GifRect s_box, s_ofs;

static void displayItem(int dispX, int dispW)
{
    GifRect ofs, box;
    GifColor col = {128, 128, 128, 0x80};
    const int texU = 0, texV = 0, texW = 128, texH = 64, dispY = 40;
    int full;

    ofs.x = (texU << 4) + 8;
    ofs.y = (texV << 4) + 8;
    ofs.w = texW << 4;
    ofs.h = texH << 4;
    box.w = dispW != 0 ? dispW << 4 : ofs.w;
    box.h = ofs.h >> 1;
    box.x = (dispX - 320) * 16;
    box.y = (dispY - 113) * 16;
    full = lt_rect_spans_grid(box.x, box.w); /* layout_texture.c: decided on the layout data */

    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 7, 0);
    box.y = box.y + 4;
    box.x = box.x + 4;
    gif_SetGsReg(20, 96);
    box.h = box.h - 16;
    ofs.h = ofs.h - 16;
    box.w = box.w - 16;
    ofs.w = ofs.w - 16;
    if (full) {
        gif_FullFrameBegin();
    }
    gif_SpriteSensitiveOffset(&box, 0xFFFFFF9B, &ofs, &col, 1);
    if (full) {
        gif_FullFrameEnd();
    }
    /* lt_glow_sprite with s = 0.5 */
    {
        GifRect rr = box;
        GifColor c = {27, 40, 57, 127};

        rr.x = rr.x - 16;
        rr.y = rr.y - 16;
        rr.w = rr.w + 32;
        rr.h = rr.h + 32;
        gif_SetAlpha(1, 5, 0);
        if (full) {
            gif_FullFrameBegin();
        }
        gif_SpriteSensitiveOffset(&rr, 0xFFFFFF9B, &ofs, &c, 1);
        if (full) {
            gif_FullFrameEnd();
        }
    }
    gif_SetZWrite(1);
    gif_EndPacket();
    s_box = box;
    s_ofs = ofs;
}

static void displayTexture(void)
{
    displayItem(100, 0); /* a menu row: does not span the grid */
}

/* the head GsBase.c's hook records, then the frame */
static void recordFrame(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};

    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    texSetTexReg(11);
    ltDrawPrimarySprite();
    displayTexture();
    dl_Swap();
}

/* ------------------------------------------------------------ the checks */

typedef struct Walk {
    int screens;
    const RdCmd *scr[8];
    RdStateBlock st[8];
} Walk;

static void collect(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *s)
{
    Walk *w = user;
    (void)index;
    if (list == 11 && c->type == RDC_SCREEN && w->screens < 8) {
        w->scr[w->screens] = c;
        w->st[w->screens] = *s;
        w->screens++;
    }
}

static int gs16(int v)
{
    return v & 0xFFFF;
}

static void checkRecording(const RdFrame *f)
{
    Walk w;
    memset(&w, 0, sizeof(w));
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    CHECK(w.screens == 3, "list 11 holds %d screen batches, expected 3", w.screens);
    if (w.screens != 3) {
        return;
    }
    const RdScreenVtx *v;

    /* 1: lt_draw_primary_sprite, untextured */
    v = (const RdScreenVtx *)(f->payload + w.scr[0]->u[0]);
    CHECK(w.scr[0]->b[0] == RD_PRIM_SPRITES && w.scr[0]->u[1] == 2, "primary: one sprite");
    CHECK(w.scr[0]->b[1] == RD_SPACE_FULLFRAME, "primary: full-frame space");
    {
        /* gif_SpriteSensitive: x * 512 / 640 in 1/16 px, + 0x8000 */
        int x0 = kPrimaryRect.x * 512 / 640 + 0x8000, y0 = kPrimaryRect.y * 512 / 224 + 0x8000;
        int x1 = x0 + kPrimaryRect.w * 512 / 640, y1 = y0 + kPrimaryRect.h * 512 / 224;
        CHECK(v[0].x == gs16(x0) && v[0].y == gs16(y0) && v[1].x == gs16(x1) && v[1].y == gs16(y1),
              "primary corners %d,%d %d,%d", v[0].x, v[0].y, v[1].x, v[1].y);
        CHECK(v[1].z == 0xFFFFFFFFu, "primary Z %08x", v[1].z);
        CHECK(memcmp(v[1].rgba, &kPrimaryCol, 4) == 0, "primary colour");
    }
    const RdStateBlock *s0 = &w.st[0];
    CHECK(!s0->ds.texEnabled, "primary: texture off");
    CHECK(s0->ds.abe == 1 && s0->ds.blend == RD_BLEND_LERP_AS && s0->ds.pabe == 0,
          "primary: ABE %d ALPHA %d PABE %d", s0->ds.abe, s0->ds.blend, s0->ds.pabe);
    CHECK(s0->ds.test.zte == 1 && s0->ds.test.ztst == RD_ZTST_ALWAYS && !s0->ds.test.ate,
          "primary: TEST 0x30000");
    CHECK(s0->ds.zwrite == RD_ZWRITE_OFF, "primary: Z write off");
    CHECK(s0->color == RD_TARGET_SCENE + 1, "primary: into SCENE");

    /* 2: display_texture's item, textured through the seam */
    v = (const RdScreenVtx *)(f->payload + w.scr[1]->u[0]);
    const RdStateBlock *s1 = &w.st[1];
    CHECK(w.scr[1]->b[1] == RD_SPACE_UI && w.scr[1]->b[2] == 1, "item: UI, FST UVs");
    CHECK(s1->ds.texEnabled && s1->tex == gif_HostPlaceholder(TEST_TBP).id,
          "item: the placeholder for TBP 0x%x bound (tex %u)", TEST_TBP, s1->tex);
    CHECK(s1->ds.tcc == RD_TCC_RGBA && s1->ds.texFn == RD_TEXFN_MODULATE,
          "item: TCC RGBA, MODULATE");
    CHECK(s1->ds.magFilter == RD_FILTER_LINEAR && s1->ds.minFilter == RD_FILTER_LINEAR,
          "item: TEX1 96 is linear/linear");
    {
        /* gif_SpriteSensitiveOffset: center_X * 16 + x * 512 / 640 */
        int x0 = 0x8000 + s_box.x * 512 / 640, y0 = 0x8000 + s_box.y * 512 / 224;
        int x1 = x0 + s_box.w * 512 / 640, y1 = y0 + s_box.h * 512 / 224;
        CHECK(v[0].x == gs16(x0) && v[0].y == gs16(y0) && v[1].x == gs16(x1) && v[1].y == gs16(y1),
              "item corners %d,%d %d,%d", v[0].x, v[0].y, v[1].x, v[1].y);
        CHECK(v[0].s == (float)s_ofs.x && v[0].t == (float)s_ofs.y &&
                  v[1].s == (float)(s_ofs.x + s_ofs.w) && v[1].t == (float)(s_ofs.y + s_ofs.h),
              "item UVs %g,%g %g,%g", v[0].s, v[0].t, v[1].s, v[1].t);
        CHECK(v[1].z == 0xFFFFFF9Bu, "item Z %08x", v[1].z);
    }
    CHECK(s1->ds.blend == RD_BLEND_LERP_AS, "item: ALPHA 0x44 (modes 4 and 7 are one register)");

    /* 3: the glow, additive */
    CHECK(w.st[2].ds.blend == RD_BLEND_CS_AS_ADD_CD && w.st[2].ds.abe == 1,
          "glow: ALPHA 0x48 with ABE (%d)", w.st[2].ds.blend);
    CHECK(f->endState.ds.zwrite == RD_ZWRITE_ON, "Z write back on at the end");
    CHECK(gif_HostUndecodedTotal() == 0, "%u undecoded register writes", gif_HostUndecodedTotal());
}

static void checkPixels(void)
{
    uint32_t w = 0, h = 0;
    uint8_t *px = malloc(512 * 512 * 4);
    if (!px) {
        failures++;
        return;
    }
    if (!rd__read_target(rd_target(RD_TARGET_SCENE), px, 512 * 512 * 4, &w, &h) || w != 512) {
        CHECK(0, "SCENE readback");
        free(px);
        return;
    }
    /* the primary sprite covers pixels 128..383 in both axes */
    const uint8_t *in = &px[(256 * 512 + 256) * 4];
    const uint8_t *out = &px[(40 * 512 + 450) * 4];
    CHECK(in[0] == 200 && in[1] == 100 && in[2] == 50, "pixel inside the sprite %u,%u,%u", in[0],
          in[1], in[2]);
    CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0, "pixel outside %u,%u,%u", out[0], out[1],
          out[2]);
    free(px);
}

/* ----------------------------------------- the full-frame decision, 16:9 */

/* the frame at 16:9 (688 GS pixels across; gif_HostSetFrameWidth as
   GsBase.c's latch calls it): the primary sprite at its real size, a menu
   row and a bar whose grid rect spans 0 .. 640 */
static void recordWide(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    GifRect r = {-5120, -1808, 10240, 3616};
    GifColor col = kPrimaryCol;

    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    texSetTexReg(11);
    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 7, 0);
    gif_FullFrameBegin();
    gif_SpriteSensitive(&r, 0xFFFFFFFF, (void *)0, &col, 1);
    gif_FullFrameEnd();
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_EndPacket();
    displayItem(100, 0); /* the row: UI */
    displayItem(0, 640); /* the bar: spans */
    dl_Swap();
}

static void checkWide(void)
{
    const int FW = 688;
    Walk w;
    gif_HostSetFrameWidth(FW);
    CHECK(lt_rect_spans_grid(-320 * 16, 640 * 16) &&
              !lt_rect_spans_grid((100 - 320) * 16, 128 * 16) &&
              !lt_rect_spans_grid(-320 * 16 + 4, 640 * 16) &&
              !lt_rect_spans_grid(-320 * 16, 640 * 16 - 1) &&
              lt_rect_spans_grid(-330 * 16, 700 * 16),
          "the bars span the grid, a menu row does not, a rect 1/16 short does not");
    recordWide();
    const RdFrame *f = rd__last_frame();
    memset(&w, 0, sizeof(w));
    if (!f) {
        CHECK(0, "a closed wide frame");
        gif_HostSetFrameWidth(512);
        return;
    }
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    CHECK(w.screens == 5, "wide: %d screen batches, expected 5 (primary, row, glow, bar, glow)",
          w.screens);
    if (w.screens == 5) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w.scr[0]->u[0]);
        const int c0 = 0x8000, half = FW / 2 * 16;
        CHECK(w.scr[0]->b[1] == RD_SPACE_FULLFRAME, "wide: the primary sprite is full-frame");
        CHECK(v[0].x == gs16(c0 - half) && v[1].x == gs16(c0 + half),
              "wide: the primary sprite covers the native width, GS 2048 -+ 344 (%d, %d)", v[0].x,
              v[1].x);
        /* the menu row: UI, the 512 / 640 box about the centre at any width */
        v = (const RdScreenVtx *)(f->payload + w.scr[1]->u[0]);
        CHECK(w.scr[1]->b[1] == RD_SPACE_UI && w.scr[2]->b[1] == RD_SPACE_UI,
              "wide: a menu row and its glow stay UI-tagged");
        {
            const int x0 = 0x8000 + ((100 - 320) * 16 + 4) * 512 / 640;
            CHECK(v[0].x == gs16(x0), "wide: the row stays in the centred 4:3 box (%d, want %d)",
                  v[0].x, gs16(x0));
        }
        /* the bar: full-frame, x scaled by the frame's width over 640 */
        v = (const RdScreenVtx *)(f->payload + w.scr[3]->u[0]);
        CHECK(w.scr[3]->b[1] == RD_SPACE_FULLFRAME && w.scr[4]->b[1] == RD_SPACE_FULLFRAME,
              "wide: the spanning bar and its glow are full-frame-tagged");
        {
            const int bx = -320 * 16 + 4, bw = 640 * 16 - 16;
            const int x0 = 0x8000 + bx * FW / 640, x1 = x0 + bw * FW / 640;
            CHECK(v[0].x == gs16(x0) && v[1].x == gs16(x1),
                  "wide: the bar is scaled by 688 / 640 (%d, %d, want %d, %d)", v[0].x, v[1].x,
                  gs16(x0), gs16(x1));
            CHECK(x0 <= c0 - half + 12 && x1 >= c0 + half - 12 * FW / 640 - 12,
                  "wide: the bar reaches within its inset of both edges");
        }
    }
    gif_HostSetFrameWidth(512);
}

/* a selected full-frame item's sparkle points (display_texture's
   gif_PointOffset loop, inside the item's gif_FullFrameBegin scope) at a
   native frame 688 wide: the grid's 0 and its right end 640 - 1 land at
   2048 and 2048 + 319 * 688 / 640 GS pixels (past the 4:3 box's 2048 + 256),
   full-frame-tagged, as the item they sparkle on is */
static void checkSparkleWide(void)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    const int FW = 688;
    GifColor pcol = {0, 0, 0, 100};
    int pt0[2] = {0, 0}, pt1[2] = {319 * 16, 0};
    Walk w;

    gif_HostSetFrameWidth(FW);
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_FullFrameBegin();
    gif_PointOffset(pt0, 0x800000, (unsigned char *)&pcol, 1);
    gif_PointOffset(pt1, 0x800000, (unsigned char *)&pcol, 1);
    gif_FullFrameEnd();
    gif_EndPacket();
    dl_Swap();
    const RdFrame *f = rd__last_frame();
    memset(&w, 0, sizeof(w));
    if (!f) {
        CHECK(0, "sparkle: a closed wide frame");
        gif_HostSetFrameWidth(512);
        return;
    }
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    int xs[2], n = 0, full = 1;
    for (int i = 0; i < w.screens; i++) {
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w.scr[i]->u[0]);
        full &= w.scr[i]->b[0] == RD_PRIM_POINTS && w.scr[i]->b[1] == RD_SPACE_FULLFRAME;
        for (uint32_t k = 0; k < w.scr[i]->u[1] && n < 2; k++) {
            xs[n++] = v[k].x;
        }
    }
    CHECK(n == 2 && full, "sparkle: %d points, full-frame-tagged points %d", n, full);
    if (n == 2) {
        const int x1 = 0x8000 + 319 * 16 * FW / 640;
        CHECK(xs[0] == gs16(0x8000) && xs[1] == gs16(x1) && x1 > 0x8000 + 256 * 16,
              "sparkle: the grid's 0 and 319 at %d and %d (want %d and %d: the native frame's "
              "width over 640)",
              xs[0], xs[1], gs16(0x8000), gs16(x1));
    }
    gif_HostSetFrameWidth(512);
}

/* ------------------------------------------ the boot signs' backdrop */

/* kanbanExec's backdrop (common/src/kanban.c, compiled into this test) at a
   native frame W GS pixels wide: sign 2, a layout with no items, black at
   colA 1 (the presents sign's backdrop in the v289 dump: 0, 0, 0, 127).
   The sprite is the 640 x 224 grid, the PS2's whole picture, so it is a
   full-frame item: x is 2048 -+ W / 2 (at 512 the UI box's numbers), y the
   grid's 224 lines over the 512-line test frame. */
static void checkKanbanBackdrop(int fw)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    Walk w;
    const RdFrame *f;
    Kanban *k;

    memset(texLayout, 0, sizeof(texLayout));
    texLayout[2].first = texLayout[2].last = 0;
    texLayout[2].fadeInTime = 1.0f;
    texLayout[2].colA = 1.0f;
    texLayout[2].defaultItem = -1;
    systemStatus[0] = 0;
    systemStatus[1] = 1;
    kanbanReqAllDel();
    k = kanbanReqAdd(2, 1);
    CHECK(k != NULL, "kanban %d: the sign is added", fw);
    gif_HostSetFrameWidth(fw);
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    kanbanExec();
    dl_Swap();
    gif_HostSetFrameWidth(512);
    kanbanReqAllDel();
    f = rd__last_frame();
    if (!f) {
        CHECK(0, "kanban %d: a closed frame", fw);
        return;
    }
    memset(&w, 0, sizeof(w));
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    CHECK(w.screens == 1, "kanban %d: %d screen batches, expected 1 (the backdrop)", fw, w.screens);
    if (w.screens != 1) {
        return;
    }
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + w.scr[0]->u[0]);
    const int c0 = 0x8000, half = fw / 2 * 16;
    const int y0 = c0 - 1792 * 512 / 224, y1 = y0 + 3584 * 512 / 224;
    CHECK(w.scr[0]->b[0] == RD_PRIM_SPRITES && w.scr[0]->u[1] == 2, "kanban %d: one sprite", fw);
    CHECK(w.scr[0]->b[1] == RD_SPACE_FULLFRAME, "kanban %d: the backdrop is full-frame (space %d)",
          fw, w.scr[0]->b[1]);
    CHECK(v[0].x == gs16(c0 - half) && v[1].x == gs16(c0 + half),
          "kanban %d: the backdrop covers the native width, GS 2048 -+ %d (%d, %d, want %d, %d)",
          fw, fw / 2, v[0].x, v[1].x, gs16(c0 - half), gs16(c0 + half));
    CHECK(v[0].y == gs16(y0) && v[1].y == gs16(y1), "kanban %d: the grid's 224 lines (%d, %d)", fw,
          v[0].y, v[1].y);
    CHECK(v[1].rgba[0] == 0 && v[1].rgba[1] == 0 && v[1].rgba[2] == 0 && v[1].rgba[3] == 127,
          "kanban %d: black at alpha 127 (%u,%u,%u,%u)", fw, v[1].rgba[0], v[1].rgba[1],
          v[1].rgba[2], v[1].rgba[3]);
    CHECK(w.st[0].ds.abe == 1 && w.st[0].ds.blend == RD_BLEND_LERP_AS && !w.st[0].ds.texEnabled,
          "kanban %d: untextured, ALPHA 0x44", fw);
}

/* ------------------------------------------ the title exit's cover */

/* kanbanTitleCover (common/src/kanban.c, PC port v0.6.2) records one frame
   at a native width fw and returns the walk of it: the screen batches it
   drew, and the first one's vertices in *vtx. */
static int last_scr_space;

static int coverFrame(int fw, const RdScreenVtx **vtx)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    static Walk w;
    const RdFrame *f;

    gif_HostSetFrameWidth(fw);
    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    kanbanTitleCover();
    dl_Swap();
    gif_HostSetFrameWidth(512);
    memset(&w, 0, sizeof(w));
    *vtx = NULL;
    f = rd__last_frame();
    if (!f) {
        return -1;
    }
    RdStateBlock s = f->startState;
    rd__walk(f, 0, &s, collect, &w);
    if (w.screens > 0) {
        *vtx = (const RdScreenVtx *)(f->payload + w.scr[0]->u[0]);
        last_scr_space = w.scr[0]->b[1];
    }
    return w.screens;
}

/* the cover is up from systemStatus[6] until the next fade is at 128 and
   falling, in the frame it records, and only then; the boot has none */
static void checkTitleCover(int fw)
{
    const RdScreenVtx *v;
    int n;

    systemStatus[6] = 0;
    kanbanBootEnd = 1;
    fadeStatus = 0;
    fadeSpeed = 0.0f;
    fadeColor[3] = 0;
    n = coverFrame(fw, &v);
    CHECK(n == 0, "cover %d: none with no stage change (%d batches)", fw, n);

    systemStatus[6] = 1;
    kanbanBootEnd = 0;
    n = coverFrame(fw, &v);
    CHECK(n == 0, "cover %d: none during the boot (%d batches)", fw, n);

    kanbanBootEnd = 1;
    ico_stage_change_to = 5;
    n = coverFrame(fw, &v);
    CHECK(n == 0, "cover %d: none for a change to another stage (%d batches)", fw, n);

    ico_stage_change_to = 1;
    fadeStatus = 2; /* End Game's fade-out: 0.025 never shows */
    fadeSpeed = 0.025f;
    fadeColor[3] = 0;
    n = coverFrame(fw, &v);
    CHECK(n == 1, "cover %d: one batch while the stage change is in flight (%d)", fw, n);
    if (n == 1 && v) {
        const int c0 = 0x8000, half = fw / 2 * 16;
        const int y0 = c0 - 1792 * 512 / 224, y1 = y0 + 3584 * 512 / 224;
        CHECK(last_scr_space == RD_SPACE_FULLFRAME, "cover %d: full-frame tagged", fw);
        CHECK(v[0].x == gs16(c0 - half) && v[1].x == gs16(c0 + half),
              "cover %d: spans the native width, GS 2048 -+ %d (%d, %d, want %d, %d)", fw, fw / 2,
              v[0].x, v[1].x, gs16(c0 - half), gs16(c0 + half));
        CHECK(v[0].y == gs16(y0) && v[1].y == gs16(y1), "cover %d: the grid's 224 lines (%d, %d)",
              fw, v[0].y, v[1].y);
        CHECK(v[1].rgba[0] == 0 && v[1].rgba[1] == 0 && v[1].rgba[2] == 0 && v[1].rgba[3] == 128,
              "cover %d: opaque black, alpha 128 (%u,%u,%u,%u)", fw, v[1].rgba[0], v[1].rgba[1],
              v[1].rgba[2], v[1].rgba[3]);
    }

    /* the load ends: the title's fade has not started yet, the cover stays */
    systemStatus[6] = 0;
    n = coverFrame(fw, &v);
    CHECK(n == 1, "cover %d: stays until the title's fade is black (%d)", fw, n);

    /* the title's fade: level 144, so fadeColor[3] 128 and falling */
    fadeStatus = 2;
    fadeSpeed = -4.0f;
    fadeColor[3] = 128;
    n = coverFrame(fw, &v);
    CHECK(n == 0 && kanbanTitleCoverActive() == 0, "cover %d: ends at the title's fade (%d)", fw,
          n);

    n = coverFrame(fw, &v);
    CHECK(n == 0, "cover %d: stays off afterwards (%d)", fw, n);
    fadeStatus = 0;
    fadeSpeed = 0.0f;
    fadeColor[3] = 0;
    kanbanBootEnd = 0;
}

/* ------------------------------------------------- keyed layout rows */

/* display_texture's row sprite under gif_HostDrawKey (layout_texture.c's
   LT_HOST_KEY: the row's texProperty entry), sliding 32 px and fading out,
   and the sparkle-like sprite after it unkeyed; frame k = 0, 1 */
static const char kRow;

static void recordKeyedRow(int k)
{
    static const uint8_t black[4] = {0, 0, 0, 0x80};
    GifRect box = {(-100 + k * 32) * 16, -20 * 16, 64 * 16, 16 * 16};
    GifRect dot = {(k ? 80 : 40) * 16, 40 * 16, 4 * 16, 4 * 16};
    GifColor col = {128, 128, 128, k ? 0 : 0x80};
    GifColor grey = {90, 90, 90, 0x80};

    dl_SetDLPriority(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), 512, 512, 1);
    rd_clear_target(rd_target(RD_TARGET_SCENE), black, 1, 0);
    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 7, 0);
    gif_HostDrawKey(&kRow, 0, 0);
    gif_SpriteSensitive(&box, 0xFFFFFFFF, (void *)0, &col, 1);
    gif_HostDrawKey(0, 0, 0);
    gif_SpriteSensitive(&dot, 0xFFFFFFFF, (void *)0, &grey, 1);
    gif_SetZWrite(1);
    gif_EndPacket();
    dl_Swap();
}

static const RdCmd *screenCmd(const RdFrame *f, int nth)
{
    for (uint32_t i = 0; f && i < f->lists[11].count; i++) {
        const RdCmd *c = &f->lists[11].cmds[i];
        if (c->type == RDC_SCREEN && nth-- == 0) {
            return c;
        }
    }
    return NULL;
}

static void checkKeyedRow(void)
{
    recordKeyedRow(0);
    recordKeyedRow(1);
    const RdFrame *prev = rd__prev_frame(), *cur = rd__last_frame();
    CHECK(prev && cur, "two layout frames retained");
    if (!prev || !cur) {
        return;
    }
    RdInterpStats st;
    const RdFrame *f = rd__interp_frame(prev, cur, 0.5f, 1, &st);
    CHECK(f && st.snap == RD_SNAP_NONE && st.keyed == 1 && st.lerped == 1,
          "the keyed row blends (snap %u keyed %u lerped %u)", st.snap, st.keyed, st.lerped);
    const RdCmd *r = screenCmd(f, 0), *rp = screenCmd(prev, 0), *rc = screenCmd(cur, 0);
    const RdCmd *d = screenCmd(f, 1), *dc = screenCmd(cur, 1);
    CHECK(r && rp && rc && d && dc && r->u[1] == 2 && d->u[1] == 2,
          "the row and the dot are separate draws");
    if (!r || !rp || !rc || !d || !dc) {
        return;
    }
    CHECK(r->keyLo != 0 || r->keyHi != 0, "the row carries the key");
    CHECK(d->keyLo == 0 && d->keyHi == 0, "the dot after the key ends is unkeyed");
    const RdScreenVtx *v = (const RdScreenVtx *)(const void *)(f->payload + r->u[0]);
    const RdScreenVtx *vp = (const RdScreenVtx *)(const void *)(prev->payload + rp->u[0]);
    const RdScreenVtx *vc = (const RdScreenVtx *)(const void *)(cur->payload + rc->u[0]);
    CHECK(v[0].x == (vp[0].x + vc[0].x) / 2 && v[1].x == (vp[1].x + vc[1].x) / 2 &&
              vc[0].x != vp[0].x && v[0].rgba[3] == 0x40,
          "the row half way (x %d between %d and %d, alpha 0x%02x)", v[0].x, vp[0].x, vc[0].x,
          v[0].rgba[3]);
    CHECK(memcmp(f->payload + d->u[0], cur->payload + dc->u[0], 2 * sizeof(RdScreenVtx)) == 0,
          "the unkeyed dot is the current frame's");
}

int main(void)
{
    rd__set_not_implemented_fatal(true); /* a stub command replayed stops the test */
    /* recording */
    if (!rd__init_record_only(512, 512)) {
        printf("FAIL rd__init_record_only\n");
        return 1;
    }
    dl_Init();
    recordFrame();
    const RdFrame *f = rd__last_frame();
    CHECK(f != NULL, "a closed frame");
    if (f) {
        checkRecording(f);
    }
    checkKeyedRow();
    checkWide();
    checkSparkleWide();
    checkKanbanBackdrop(2048);
    checkKanbanBackdrop(512);
    checkTitleCover(ico_frame_width_for(16.0f / 9.0f));
    checkTitleCover(ico_frame_width_for(48.0f / 9.0f));
    checkTitleCover(512);
    rd_shutdown();
    if (failures) {
        printf("rd_layout_test: %d failures\n", failures);
        return 1;
    }

    /* replay on a device */
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = RD_PRESET_ORIGINAL;
    if (!rd_init(512, 512, &st, NULL)) {
        printf("rd_layout_test: recording ok; SKIP the pixel check: no usable Vulkan device\n");
        return 77;
    }
    gif_HostForgetTextures();
    gif_HostFrameReset();
    dl_Clear();
    recordFrame();
    checkPixels();
    CHECK(rhi_vk_validation_error_count() == 0, "%u validation errors",
          rhi_vk_validation_error_count());
    rd_shutdown();
    if (failures) {
        printf("rd_layout_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_layout_test: ok\n");
    return 0;
}
