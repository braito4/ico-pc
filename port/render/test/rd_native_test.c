/* rd_native_test.c: the native frame's centre rule, on a synthetic frame.
 *
 * One game frame in absolute GS geometry (every x in the 12.4 window space
 * around 2048, the PS2's projection never changes with the aspect) is
 * rendered at the native widths W = 512, 688, 1376, 2048 and 2560 (the
 * display aspect W / 384, ico_frame_width_for) through rd_reset_scene(W, 448) and the frame
 * head's gsW = W, the Original preset at 1x and the Enhanced preset at
 * scene scale 1 and 2.  The frame is the plan's Gate B in miniature
 * (v0.6.0, D5): the 4:3 picture must not know how wide the frame is.
 *
 * The geometry (kTris below and the sprites in drawFrame):
 *   - VU triangles of code 32 (the region test), 34 (no test) and 36 (the
 *     scissor program): inside the 4:3 band, crossing its left edge, its
 *     right edge, both edges, beside it only, a code-36 triangle with a cut
 *     vertex (GS Z < 0), and the two a 4:3 frame drops that reach into the
 *     picture, one with a vertex far past the region window (X > 4094) and
 *     one with a vertex behind the eye (w < 0): drawn nowhere, at any W;
 *   - a textured UI sprite covering the 4:3 frame (RD_SPACE_UI, a 512 x 448
 *     texture 1:1), blended;
 *   - a full-frame tagged flat sprite (RD_SPACE_FULLFRAME), blended;
 *   - a second flat full-frame sprite standing in for the fog pass (the
 *     real RD_POST_FOG needs the depth LUT of rd_fog_test; the fog's 4:3
 *     rule is that test's and W2-SPACES' business);
 *   - the DISPLAY reduction (RD_POST_REDUCTION) at the end of list 12.
 *
 * The cases:
 *   c  every W, every preset (Original 1x, Enhanced 1x and 2x): SCENE and
 *      DISPLAY columns [(W/2 - 256) s, +512 s) equal the W = 512 render's
 *      byte for byte (Gate B's centre rule).  DISPLAY leaves out two GS
 *      pixels at each end of the 512 columns: the reduction crops its
 *      picture 2 pixels from the frame's edge and its bilinear reads one
 *      pixel past, which at W = 512 is the frame's edge and at a wider W is
 *      picture.  Exact since EXACT-RASTER (v0.6.1): before it the GPU
 *      clipped a triangle reaching past the target at the viewport (the
 *      target, whose edge is the band's at W 512), which moved its edges,
 *      and interpolated the colours from the pixel's place in the target
 *      (rd_replay.c s_vpExact, vu_common.hlsli vu_attr_planes).
 *   n  every W < 2560, every preset: SCENE equals the W = 2560 render's
 *      centre W s columns byte for byte, DISPLAY too but for two GS pixels
 *      at the narrower frame's edges (the reduction's crop): the sides
 *      continue exactly, a wider frame only adds columns.
 *   e  every W > 512, every preset: no step of SCENE across either old
 *      band edge (the column pair at (W/2 -+ 256) s) is larger than the
 *      99th percentile (+1 LSB for rounding) of the horizontal gradient of
 *      its row within 16 GS pixels of the edge, nor than the vertical
 *      steps beside it (a triangle's own edge crossing the column there),
 *      per channel; on the frame without the UI sprite (s_noUi: a UI item
 *      is the 4:3 picture's, its edge is the band's by design).
 *   p  corners and edges on pixel boundaries and pixel centres
 *      (checkBoundaryCorners), and a Gouraud screen-prim triangle (the CPU
 *      GIF path): every W, every preset, the 4:3 columns of SCENE equal the
 *      W = 512 render's byte for byte.  Failed before
 *      EXACT-RASTER at every wider W, 2048 included (the two triangles
 *      across the band edges: their pixel-centre ties on the edge the GPU
 *      clipped at the W 512 target's edge went the other way).
 *   h  W = 512, Original 1x: the hash of SCENE and of DISPLAY equal
 *      kExpect (Gate A in miniature: the Original picture at 4:3 does not
 *      change).  Runs always; kExpect is zero until the first run's printed
 *      hashes are filled in (then the check is off).
 *   s  the region rule's strip test (VIS-WINDOW, v0.6.1; vu_common.hlsli
 *      vu_region_rule), runs always, one strip a frame (checkStripRule):
 *      a wall (a 2-triangle strip whose second triangle has a vertex above
 *      the region window and reaches the picture, issue 72) is drawn as
 *      the PS2 draws it out of the box (Original, W 512, 1x: the failing
 *      triangle is not drawn) and with the failing triangle clipped in
 *      every other state (Enhanced at W 512, 688 (16:9) and 2048 (48:9),
 *      Enhanced 2x, Original at W 688); a card (a free-standing 2-triangle
 *      strip both of whose triangles fail, the opening's tree card) and a
 *      strip whose failing triangle has a vertex behind the eye add nothing
 *      in any state; the same card's strip joined to a passing strip (a
 *      plank's face, rd_mesh.c markJoinedStrips) is drawn as the wall is.
 *   t  the nearest filter at exact texel boundaries (checkGrainTexels: the
 *      film grain at W 512 to 2560; checkNearestTies: every path that
 *      takes a nearest texel, at W 512 and 688) reads the GS's texel at
 *      every pixel of the 4:3 columns.  Runs always.
 *   The W = 512 renders also have to show the frame (not the clear colour).
 * Every case runs on every run (the ICO_NATIVE_GATE switch is gone: the
 * centre rule holds since EXACT-RASTER; the whole test takes well under a
 * minute on lavapipe).
 *
 * Exit 0 all, 1 a mismatch, 77 without a Vulkan device. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "vk/rhi_vk.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* The recorded expectation of the Original preset at W = 512, 1x: FNV-1a 64
 * of SCENE's and of DISPLAY's readback bytes (lavapipe).  It moved from
 * v0.6.0's for two causes:
 *   - the nearest filter's rounding to the GS's texel at texel boundaries
 *     (GRAIN-PHASE, common.hlsli gs_nearest_uv); after it the hash was
 *     ed09df2a0e3e1074 / cd51a5ce8f28f653;
 *   - EXACT-RASTER: the exact viewport (no GPU clip at the 512 target's
 *     edge: 58 pixel-centre ties on the edges of the triangles reaching
 *     past it go to the unclipped triangle's side) and the attribute
 *     planes (23 pixels one colour step apart from the GPU's
 *     interpolation). */
static const uint64_t kExpect[2] = {0xd7c36bf58c6091f6ull, 0x410c22223b5aa4baull};

static const int kWidths[] = {512, 688, 1376, 2048, 2560};
#define NWIDTHS ((int)(sizeof(kWidths) / sizeof(kWidths[0])))
#define GS_H 448
#define BAND 512 /* the 4:3 frame's GS width */

typedef struct Cfg {
    const char *name;
    RdPreset preset;
    float scale;
} Cfg;

static const Cfg kCfgs[] = {
    {"Original 1x", RD_PRESET_ORIGINAL, 1.0f},
    {"Enhanced 1x", RD_PRESET_ENHANCED, 1.0f},
    {"Enhanced 2x", RD_PRESET_ENHANCED, 2.0f},
};
#define NCFGS ((int)(sizeof(kCfgs) / sizeof(kCfgs[0])))

typedef struct Img {
    uint8_t *px;
    uint32_t w, h;
} Img;

typedef struct Render {
    Img scene, display;
} Render;

/* ------------------------------------------------------------ geometry */

/* One VU triangle in absolute GS coordinates (x, y: GS pixels of the 12.4
 * window around 2048, so the 4:3 band is x 1792..2304 and the rows are
 * y 1824..2272; z: GS Z; w: 1, or -1 behind the eye, where the divide makes
 * the GS x -x, so x is given negated).  code: the MSCALF code (32 region
 * test, 34 none, 36 scissor).  Colours per vertex, RGB. */
typedef struct Tri {
    const char *what;
    int code;
    float v[3][4];
    uint8_t c[3][3];
} Tri;

static const Tri kTris[] = {
    {"inside the band",
     32,
     {{1900, 1900, 1000000, 1}, {2200, 1950, 1000000, 1}, {2050, 2200, 1000000, 1}},
     {{230, 40, 40}, {40, 230, 40}, {40, 40, 230}}},
    {"crossing the left edge",
     32,
     {{1500, 1850, 1010000, 1}, {2000, 1900, 1010000, 1}, {1700, 2250, 1010000, 1}},
     {{200, 200, 40}, {40, 200, 200}, {200, 40, 200}}},
    {"crossing the right edge",
     32,
     {{2100, 1850, 1020000, 1}, {2700, 2000, 1020000, 1}, {2150, 2260, 1020000, 1}},
     {{240, 140, 40}, {140, 240, 40}, {40, 140, 240}}},
    {"spanning both edges",
     32,
     {{1200, 1830, 990000, 1}, {2900, 1900, 990000, 1}, {2000, 2260, 990000, 1}},
     {{90, 90, 160}, {160, 90, 90}, {90, 160, 90}}},
    {"beside the band, right",
     32,
     {{2400, 1900, 1030000, 1}, {2900, 1950, 1030000, 1}, {2600, 2250, 1030000, 1}},
     {{250, 250, 250}, {120, 120, 250}, {250, 120, 120}}},
    {"no region test (34), in front",
     34,
     {{1950, 2000, 1040000, 1}, {2100, 2050, 1040000, 1}, {2000, 2150, 1040000, 1}},
     {{255, 255, 255}, {200, 200, 255}, {255, 200, 200}}},
    {"scissor program (36), a cut vertex (GS Z < 0)",
     36,
     {{2000, 1850, -2000000, 1}, {2250, 2200, 1000000, 1}, {1850, 2150, 1000000, 1}},
     {{60, 220, 160}, {220, 160, 60}, {160, 60, 220}}},
    {"far past the region window (X > 4094), reaches the picture",
     32,
     {{2000, 1900, 1005000, 1}, {2200, 2100, 1005000, 1}, {4300, 2000, 1005000, 1}},
     {{255, 0, 255}, {255, 0, 255}, {255, 0, 255}}},
    {"behind the eye (w < 0), reaches the picture",
     32,
     {{1950, 1900, 1006000, 1}, {2150, 2200, 1006000, 1}, {-2300, 2000, 1006000, -1}},
     {{0, 255, 255}, {0, 255, 255}, {0, 255, 255}}},
};
#define NTRIS ((int)(sizeof(kTris) / sizeof(kTris[0])))

static const char kKey;

static RdMesh triMesh(const Tri *t)
{
    float qw[1 + 3 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8003u; /* NLOOP 3, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < 3; k++) {
        memcpy(qw[1 + k * 3], t->v[k], sizeof(t->v[k]));
        qw[1 + k * 3 + 1][2] = 1.0f;                 /* Q */
        qw[1 + k * 3 + 1][3] = k == 0 ? 0.0f : 1.0f; /* the strip flag: starts at 0 */
        for (int i = 0; i < 3; i++) {
            qw[1 + k * 3 + 2][i] = (float)t->c[k][i];
        }
        qw[1 + k * 3 + 2][3] = 127.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = 1 + 3 * 3;
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    const RdMesh m = rd_create_vu_mesh(&md);
    CHECK(m.id != 0, "rd_create_vu_mesh (%s)", t->what);
    return m;
}

/* the identity model-to-screen matrix: a vertex's x, y are GS pixels as given */
static void triDraw(RdMesh m, const Tri *t, int i)
{
    RdVuDraw d;
    memset(&d, 0, sizeof(d));
    d.prog = RD_PROG_PRELIT;
    d.code = (uint8_t)t->code;
    d.clip = t->code == 34   ? RD_VU_CLIP_NONE
             : t->code == 36 ? RD_VU_CLIP_SCISSOR
                             : RD_VU_CLIP_REGION;
    for (int c = 0; c < 4; c++) {
        d.vu.mem[16 + c][c] = 1.0f;
    }
    /* clip space (mem[20..23]): x = y = 0, w = pos.w, z = pos.z / 1.02M */
    d.vu.mem[23][3] = 1.0f;
    d.vu.mem[22][2] = 1.0f / 1020000.0f;
    rd_draw_vu_mesh(m, &d, RD_KEY(&kKey, i, 0));
}

/* ------------------------------------------------------------- sprites */

static RdScreenVtx vtx(int32_t x, int32_t y, const uint8_t c[4], int32_t u, int32_t v)
{
    RdScreenVtx s;
    memset(&s, 0, sizeof(s));
    s.x = x;
    s.y = y;
    s.z = 0;
    s.s = (float)u;
    s.t = (float)v;
    s.q = 1.0f;
    memcpy(s.rgba, c, 4);
    return s;
}

/* a sprite from GS pixel (x0, y0) to (x1, y1), absolute, UVs in 12.4 texels */
static void spriteAbs(int x0, int y0, int x1, int y1, const uint8_t c[4], RdSpace space, int32_t u0,
                      int32_t v0, int32_t u1, int32_t v1)
{
    RdScreenVtx v[2] = {vtx(x0 * 16, y0 * 16, c, u0, v0), vtx(x1 * 16, y1 * 16, c, u1, v1)};
    rd_screen_prims(RD_PRIM_SPRITES, v, 2, space, 1, 0);
}

static int s_grain;

/* 1: drawFrame leaves out the UI sprite (the continuity case e) */
static int s_noUi;

/* checkNearestTies' case: the texture (kind), the filters or TEX1, the
 * level the pixels read */
typedef struct TieCase {
    int kind; /* 0 one level RGBA32, 1 RGBA16 under 7F/81+AEM, 2 three GS levels */
    RdFilter mag, min;
    uint64_t tex1; /* kind 2: TEX1 (MMAG, MMIN, MXL 2, LCM 1, K) */
    int lv;
    const char *what;
} TieCase;

static const TieCase *s_tie;
static RdTex s_tieTex;

/* s_tie's texture: each texel holds its own coordinates (tieDecode) */
static RdTex tieTexture(void)
{
    enum { N = 64 };

    static uint8_t px[3][N * N * 4];
    static const uint8_t mark[3] = {0, 200, 100};
    RdTexImage ims[3];
    memset(ims, 0, sizeof(ims));
    for (int l = 0; l < 3; l++) {
        const int n = N >> l;
        for (int y = 0; y < n; y++) {
            for (int x = 0; x < n; x++) {
                uint8_t *p = &px[l][(y * n + x) * 4];
                if (s_tie->kind == 1) {
                    p[0] = (uint8_t)((x & 31) << 3);
                    p[1] = (uint8_t)((y & 31) << 3);
                    p[2] = (uint8_t)(((x >> 5) | ((y >> 5) << 1)) << 3);
                    p[3] = 1;
                } else {
                    p[0] = (uint8_t)x;
                    p[1] = (uint8_t)y;
                    p[2] = mark[l];
                    p[3] = 0x80;
                }
            }
        }
        ims[l].w = ims[l].h = (uint32_t)n;
        ims[l].psm = RDTEX_PSMCT32;
        ims[l].pixels = px[l];
    }
    if (s_tie->kind == 1) {
        return rd_create_texture_src(N, N, px[0], RD_TEXSRC_RGBA16, "ties rgba16");
    }
    if (s_tie->kind == 2) {
        return rdtex_store_levels(9003, 1, RDTEX_TEXA_REPLAY, ims, 3, NULL, "ties gs levels");
    }
    return rd_create_texture(N, N, px[0], RD_TEXA_80_80, "ties one level");
}

static RdTex makeTexture(void)
{
    static uint8_t px[512 * 512 * 4];
    for (int y = 0; y < 512; y++) {
        for (int x = 0; x < 512; x++) {
            uint8_t *p = &px[((size_t)y * 512 + (size_t)x) * 4];
            const int chk = ((x >> 3) ^ (y >> 3)) & 1;
            p[0] = (uint8_t)(x / 2);
            p[1] = (uint8_t)(y / 2);
            p[2] = (uint8_t)(chk ? 220 : 60);
            p[3] = (uint8_t)(chk ? 0x60 : 0x28);
            if (s_grain == 2) {
                /* the texel's own coordinates, opaque (texelAt) */
                p[0] = (uint8_t)(x & 255);
                p[1] = (uint8_t)(y & 255);
                p[2] = (uint8_t)((x >> 8) | ((y >> 8) << 1));
                p[3] = 0x80;
            }
        }
    }
    return rd_create_texture(512, 512, px, RD_TEXA_80_80, "rd_native");
}

/* ----------------------------------------------------------- the frame */

/* 1: drawFrame adds the film grain (RD_POST_FILM_NOISE) after the fog stand-in */
static const uint8_t kBg[4] = {0x30, 0x50, 0x70, 0x80};

/* The game frame at native width W: list 0 holds the head (the clear), the
 * VU triangles, the UI sprite and the full-frame sprites in order; list 12
 * the DISPLAY reduction. */
static void drawFrame(int W, RdTex tex, const RdMesh *meshes)
{
    const int l = 2048 - W / 2, r = 2048 + W / 2, top = 2048 - GS_H / 2, bot = 2048 + GS_H / 2;
    const int bl = 2048 - BAND / 2, br = 2048 + BAND / 2;
    rd_begin_frame();
    RdFrameHead h;
    memset(&h, 0, sizeof(h));
    memcpy(h.rgba, kBg, 4);
    h.gsW = (uint32_t)W;
    h.gsH = GS_H;
    h.clear = 1;
    rd_frame_head(&h);

    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), (uint32_t)W, GS_H, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    for (int i = 0; i < NTRIS; i++) {
        triDraw(meshes[i], &kTris[i], i);
    }

    /* the UI sprite: the 4:3 frame, a 512 x 448 part of the texture 1:1 */
    static const uint8_t grey[4] = {0x80, 0x80, 0x80, 0x80};
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(1);
    rd_gouraud(0);
    rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd_texture(tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
    if (!s_noUi) {
        spriteAbs(bl, top, br, bot, grey, RD_SPACE_UI, 8, 8, BAND * 16 + 8, GS_H * 16 + 8);
    }

    /* the full-frame items: a tint, and the fog stand-in */
    static const uint8_t tint[4] = {30, 60, 120, 0x28}, fog[4] = {0xA0, 0xB0, 0xC0, 0x18};
    rd_texture_off();
    spriteAbs(l, top, r, bot, tint, RD_SPACE_FULLFRAME, 0, 0, 0, 0);
    spriteAbs(l, top, r, bot, fog, RD_SPACE_FULLFRAME, 0, 0, 0, 0);

    if (s_grain) {
        /* gsb_filmNoise: the noise texture bound, then the post (no source
           target: it uses the bound texture), alpha 0x40 (0x80 with the
           texel texture: the texel replaces the frame), repeat 4.5: 4.5
           texels per pixel.  The GS samples a pixel at its integer
           coordinate, so every even column (4.5 x) and every other row lands
           exactly on a texel boundary, where the GS takes the texel the
           boundary starts (checkGrainTexels) */
        RdPostParams gp;
        memset(&gp, 0, sizeof(gp));
        gp.rgba[0] = gp.rgba[1] = gp.rgba[2] = 0x80;
        gp.rgba[3] = s_grain == 2 ? 0x80 : 0x40;
        gp.scalar[0] = 4.5f;
        rd_sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        rd_texture(tex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_post(RD_POST_FILM_NOISE, &gp);
    }

    if (s_tie) {
        /* checkNearestTies: an opaque sprite over the 4:3 square, UV 0..2304
           texels (4.5 a pixel) both ways, REPEAT */
        rd_abe(0);
        rd_tex_a(s_tie->kind == 1 ? RD_TEXA_7F_81_AEM : RD_TEXA_80_80);
        rd_texture(s_tieTex, RD_TEXFN_MODULATE, RD_TCC_RGBA);
        rd_sampler(s_tie->mag, s_tie->min, RD_WRAP_REPEAT, RD_WRAP_REPEAT);
        if (s_tie->kind == 2) {
            rd_sampler_tex1(s_tie->tex1);
        }
        spriteAbs(bl, bl, br, br, grey, RD_SPACE_UI, 0, 0, 2304 * 16, 2304 * 16);
        rd_tex_a(RD_TEXA_80_80);
    }

    rd_select_list(12);
    RdPostParams pp;
    memset(&pp, 0, sizeof(pp));
    pp.rgba[0] = 100;
    pp.rgba[1] = 128;
    pp.rgba[2] = 90;
    rd_post(RD_POST_REDUCTION, &pp);
    rd_end_frame(0);
}

static bool readImg(RdTargetId id, Img *o)
{
    const RdTargetRec *t = rd__target_rec(rd_target(id).id);
    memset(o, 0, sizeof(*o));
    if (!t) {
        CHECK(0, "target %d has no record", (int)id);
        return false;
    }
    o->px = malloc((size_t)t->tw * t->th * 4);
    if (!o->px || !rd__read_target(rd_target(id), o->px, (size_t)t->tw * t->th * 4, &o->w, &o->h)) {
        CHECK(0, "readback of target %d", (int)id);
        free(o->px);
        o->px = NULL;
        return false;
    }
    return true;
}

static void freeRender(Render *r)
{
    free(r->scene.px);
    free(r->display.px);
    memset(r, 0, sizeof(*r));
}

enum { R_OK = 0, R_FAIL = 1, R_NO_DEVICE = 2 };

/* the frame at native width W under cfg */
static int renderAt(const Cfg *cfg, int W, Render *out)
{
    memset(out, 0, sizeof(*out));
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = cfg->preset;
    s.outputWidth = 640;
    s.outputHeight = 480;
    s.aspect = W == BAND ? 4.0f / 3.0f : (float)W / 384.0f; /* ico_frame_width_for(aspect) == W */
    s.sceneScale = cfg->scale;
    if (!rd_init(BAND, GS_H, &s, NULL)) {
        return R_NO_DEVICE;
    }
    if (W != BAND) {
        rd_reset_scene((uint32_t)W, GS_H);
    }
    RdMesh meshes[NTRIS];
    for (int i = 0; i < NTRIS; i++) {
        meshes[i] = triMesh(&kTris[i]);
    }
    const RdTex tex = makeTexture();
    if (s_tie) {
        s_tieTex = tieTexture();
        CHECK(s_tie->kind != 2 ||
                  (rd__tex_rec(s_tieTex.id) && rd__tex_rec(s_tieTex.id)->gsLevels == 3),
              "ties: the coordinate levels kept as three GS levels");
    }
    drawFrame(W, tex, meshes);
    const bool ok =
        readImg(RD_TARGET_SCENE, &out->scene) && readImg(RD_TARGET_DISPLAY, &out->display);
    for (int i = 0; i < NTRIS; i++) {
        rd_destroy_vu_mesh(meshes[i]);
    }
    rd_destroy_texture(tex);
    if (s_tie && s_tie->kind == 2) {
        rdtex_drop(9003);
    } else if (s_tie) {
        rd_destroy_texture(s_tieTex);
    }
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "%s, W %d: %u validation errors", cfg->name, W, verr);
    rd_shutdown();
    if (!ok) {
        freeRender(out);
    }
    return ok ? R_OK : R_FAIL;
}

/* ------------------------------------------------------------- checks */

static uint64_t fnv(const uint8_t *p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

/* texels per GS pixel across; 0 when the image is not a whole number of them */
static uint32_t texelsPerPixel(const Img *im, int W)
{
    return im->w % (uint32_t)W == 0 ? im->w / (uint32_t)W : 0;
}

/* the picture must show the frame: some pixels of the band differ from the clear */
static void checkShows(const char *what, const Img *im, int W, int guard)
{
    const uint32_t f = texelsPerPixel(im, W);
    size_t differ = 0, total = 0;
    for (uint32_t y = 0; y < im->h; y++) {
        for (uint32_t x = (uint32_t)(W / 2 - BAND / 2 + guard) * f;
             x < (uint32_t)(W / 2 + BAND / 2 - guard) * f; x++) {
            const uint8_t *p = &im->px[((size_t)y * im->w + x) * 4];
            total++;
            differ += memcmp(p, kBg, 3) != 0;
        }
    }
    CHECK(f != 0 && total > 0 && differ * 2 > total,
          "%s shows the frame (%zu of %zu pixels differ "
          "from the clear)",
          what, differ, total);
}

/* Gate B's centre rule: the columns [(W/2 - 256) s, +512 s) of a equal r's
 * (a W = 512 render of the same case); guard GS pixels at each end left out */
static void compareCentre(const char *what, const Cfg *cfg, int W, const Img *a, const Img *r,
                          int guard)
{
    const uint32_t f = texelsPerPixel(a, W), fr = texelsPerPixel(r, BAND);
    if (f == 0 || f != fr || a->h != r->h) {
        CHECK(0, "%s, %s, W %d: sizes %ux%u (W 512: %ux%u)", cfg->name, what, W, a->w, a->h, r->w,
              r->h);
        return;
    }
    const uint32_t x0 = (uint32_t)(W / 2 - BAND / 2) * f;
    size_t bad = 0;
    int shown = 0;
    for (uint32_t y = 0; y < a->h; y++) {
        for (uint32_t x = (uint32_t)guard * f; x < (uint32_t)(BAND - guard) * f; x++) {
            const uint8_t *pa = &a->px[((size_t)y * a->w + x0 + x) * 4];
            const uint8_t *pr = &r->px[((size_t)y * r->w + x) * 4];
            if (memcmp(pa, pr, 4) != 0) {
                if (shown++ < 4) {
                    printf("  %s %s W %d (%u,%u): got %u %u %u %u, W 512 has %u %u %u %u\n",
                           cfg->name, what, W, x0 + x, y, pa[0], pa[1], pa[2], pa[3], pr[0], pr[1],
                           pr[2], pr[3]);
                }
                bad++;
            }
        }
    }
    CHECK(bad == 0, "%s, %s, W %d: %zu centre pixels differ from the W 512 render", cfg->name, what,
          W, bad);
}

static int cmpInt(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* no step across the old band edges larger than the 99th percentile (+1) of
 * the row's horizontal gradient within 16 GS pixels of the edge, nor than
 * the largest vertical step of the two columns at the edge between this
 * row and the next or the last (+1): where a triangle's own edge crosses
 * the column pair the picture steps across it and down it alike, a seam
 * steps only across */
static void checkContinuity(const Cfg *cfg, int W, const Img *im)
{
    const uint32_t f = texelsPerPixel(im, W);
    if (f == 0) {
        CHECK(0, "%s, W %d: SCENE width %u", cfg->name, W, im->w);
        return;
    }
    const int reach = 16 * (int)f;
    for (int side = 0; side < 2; side++) {
        const int e =
            (W / 2 + (side ? BAND / 2 : -BAND / 2)) * (int)f; /* first column of the right half */
        size_t bad = 0;
        int worst = 0;
        for (uint32_t y = 0; y < im->h; y++) {
            for (int c = 0; c < 3; c++) {
                int g[2 * 16 * 8], n = 0;
                const uint8_t *row = &im->px[(size_t)y * im->w * 4];
                for (int x = e - reach; x < e + reach; x++) {
                    if (x != e) {
                        g[n++] = abs((int)row[x * 4 + c] - (int)row[(x - 1) * 4 + c]);
                    }
                }
                qsort(g, (size_t)n, sizeof(int), cmpInt);
                const int p99 = g[(int)(0.99 * (double)(n - 1))];
                const int step = abs((int)row[e * 4 + c] - (int)row[(e - 1) * 4 + c]);
                int vert = 0;
                for (int dy = -1; dy <= 1; dy += 2) {
                    if ((int)y + dy < 0 || (int)y + dy >= (int)im->h) {
                        continue;
                    }
                    const uint8_t *nb = &im->px[(size_t)((int)y + dy) * im->w * 4];
                    for (int x = e - 1; x <= e; x++) {
                        const int v = abs((int)row[x * 4 + c] - (int)nb[x * 4 + c]);
                        vert = v > vert ? v : vert;
                    }
                }
                if (step > p99 + 1 && step > vert + 1) {
                    bad++;
                    worst = step > worst ? step : worst;
                }
            }
        }
        CHECK(bad == 0,
              "%s, W %d: %zu steps across the %s band edge exceed the row's 99th "
              "percentile (worst %d)",
              cfg->name, W, bad, side ? "right" : "left", worst);
    }
}

/* The sides continue exactly (case n): the columns of the W render are the
 * centre W s columns of the wide render's (Wide), byte for byte; guard GS
 * pixels at each end of the W frame left out */
static void compareNested(const char *what, const Cfg *cfg, int W, const Img *a, int Wide,
                          const Img *wide, int guard)
{
    const uint32_t f = texelsPerPixel(a, W), fw = texelsPerPixel(wide, Wide);
    if (f == 0 || f != fw || a->h != wide->h) {
        CHECK(0, "%s, %s, W %d in %d: sizes %ux%u (W %d: %ux%u)", cfg->name, what, W, Wide, a->w,
              a->h, Wide, wide->w, wide->h);
        return;
    }
    const uint32_t x0 = (uint32_t)(Wide / 2 - W / 2) * f;
    size_t bad = 0;
    int shown = 0;
    for (uint32_t y = 0; y < a->h; y++) {
        for (uint32_t x = (uint32_t)guard * f; x < (uint32_t)(W - guard) * f; x++) {
            const uint8_t *pa = &a->px[((size_t)y * a->w + x) * 4];
            const uint8_t *pw = &wide->px[((size_t)y * wide->w + x0 + x) * 4];
            if (memcmp(pa, pw, 4) != 0) {
                if (shown++ < 4) {
                    printf("  %s %s W %d (%u,%u): got %u %u %u %u, W %d has %u %u %u %u\n",
                           cfg->name, what, W, x, y, pa[0], pa[1], pa[2], pa[3], Wide, pw[0], pw[1],
                           pw[2], pw[3]);
                }
                bad++;
            }
        }
    }
    CHECK(bad == 0, "%s, %s, W %d: %zu pixels differ from the W %d render's same columns",
          cfg->name, what, W, bad, Wide);
}

/* The film grain spans the native frame (D12, issue 75): the 512 columns of
 * the 4:3 picture carry the same grain at every width (centre rule), and the
 * columns beside them carry grain too (they differ from the same frame
 * without it) */
static void checkFilmGrain(void)
{
    static const int widths[] = {688, 1376, 2560};
    Render ref, refNo;
    s_grain = 1;
    int st = renderAt(&kCfgs[0], BAND, &ref);
    s_grain = 0;
    if (st != R_OK || renderAt(&kCfgs[0], BAND, &refNo) != R_OK) {
        CHECK(0, "film grain: the W 512 frame could not be rendered");
        return;
    }
    CHECK(memcmp(ref.scene.px, refNo.scene.px, (size_t)ref.scene.w * ref.scene.h * 4) != 0,
          "film grain: the W 512 frame shows the grain");
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        const int W = widths[i];
        Render g, n;
        s_grain = 1;
        st = renderAt(&kCfgs[0], W, &g);
        s_grain = 0;
        if (st != R_OK || renderAt(&kCfgs[0], W, &n) != R_OK) {
            CHECK(0, "film grain: the W %d frame could not be rendered", W);
            continue;
        }
        const uint32_t f = texelsPerPixel(&g.scene, W);
        /* The centre columns carry the same grain at every width, byte for
           byte (the frame under it is the same since EXACT-RASTER) */
        {
            size_t bad = 0;
            const size_t x0 = (size_t)(W / 2 - BAND / 2) * f;
            for (uint32_t y = 0; f == 1 && y < g.scene.h; y++) {
                for (size_t x = 0; x < (size_t)BAND; x++) {
                    const size_t o = ((size_t)y * g.scene.w + x0 + x) * 4;
                    const size_t r = ((size_t)y * ref.scene.w + x) * 4;
                    bad += memcmp(&g.scene.px[o], &ref.scene.px[r], 4) != 0;
                }
            }
            CHECK(f == 1 && bad == 0,
                  "film grain, W %d: %zu centre pixels differ from the W 512 render", W, bad);
        }
        size_t differ = 0, total = 0;
        for (uint32_t y = 0; f && y < g.scene.h; y++) {
            for (uint32_t x = 0; x < (uint32_t)(W / 2 - BAND / 2) * f; x++) {
                const size_t o = ((size_t)y * g.scene.w + x) * 4;
                total++;
                differ += memcmp(&g.scene.px[o], &n.scene.px[o], 3) != 0;
            }
        }
        CHECK(total > 0 && differ * 2 > total,
              "film grain, W %d: the left side columns carry the grain (%zu of %zu pixels differ "
              "from the frame without it)",
              W, differ, total);
        freeRender(&g);
        freeRender(&n);
    }
    freeRender(&ref);
    freeRender(&refNo);
}

/* ------------------------------------------------- the strip rule (s) */

/* One or two strips of n VU vertices (code 32, the identity matrix of
 * triDraw: x, y GS pixels, z GS Z; w 1), one colour; split: the vertex the
 * second strip starts at (0: one strip).  eye: a vertex's w is its clip w
 * (-1 behind the eye, x and y then negated by the divide).  The region
 * programs transform pos with w 1 (normal_c.vsm:152-155, vf00.w), so a
 * stream w never reaches h.w: an eye mesh streams (x, y, w, 1) and is
 * drawn with the matrix h = (x, y, Z w, w), Z the vertices' GS Z (v[0][2]),
 * so h.w is the given w and every vertex in front lands at GS Z. */
typedef struct StripMesh {
    const char *what;
    int n, split, eye;
    float v[8][4];
    uint8_t c[3];
} StripMesh;

/* a wall: triangle 2 passes, triangle 3 has a vertex above the region
 * window (GS y -379, the water tower's lift wall) and reaches the
 * picture */
static const StripMesh kWall = {"wall",
                                4,
                                0,
                                0,
                                {{1900, 2250, 1000000, 1},
                                 {2200, 2250, 1000000, 1},
                                 {2050, 2000, 1000000, 1},
                                 {2300, -379, 1000000, 1}},
                                {200, 60, 60}};
/* a card: both triangles have a vertex above the window and reach the
 * picture; a free-standing strip (the opening's tree card) */
static const StripMesh kCard = {"card",
                                4,
                                0,
                                0,
                                {{1820, -100, 1000000, 1},
                                 {1900, -100, 1000000, 1},
                                 {1820, 2000, 1000000, 1},
                                 {1900, 2000, 1000000, 1}},
                                {60, 200, 60}};
/* the same card's strip joined to a passing triangle's strip below it
 * (they share the corners (1820, 2000) and (1900, 2000): a plank's face,
 * rd_mesh.c markJoinedStrips) */
static const StripMesh kJoined = {"joined card",
                                  7,
                                  3,
                                  0,
                                  {{1820, 2000, 1000000, 1},
                                   {1900, 2000, 1000000, 1},
                                   {1860, 2100, 1000000, 1},
                                   {1820, -100, 1000000, 1},
                                   {1900, -100, 1000000, 1},
                                   {1820, 2000, 1000000, 1},
                                   {1900, 2000, 1000000, 1}},
                                  {60, 200, 200}};
/* triangle 2 passes, triangle 3 has a vertex behind the eye (h.w -1) and
 * reaches the picture: what of it lies in front of the eye runs from the
 * edge 1-2, inside the picture, away from the vertex's image */
static const StripMesh kEye = {"behind the eye",
                               4,
                               0,
                               1,
                               {{1960, 1850, 1006000, 1},
                                {2030, 1850, 1006000, 1},
                                {1960, 1980, 1006000, 1},
                                {-2300, 2000, 1006000, -1}},
                               {60, 60, 200}};

static RdMesh stripMesh(const StripMesh *m, int n)
{
    float qw[1 + 8 * 3][4];
    memset(qw, 0, sizeof(qw));
    const uint32_t tag = 0x8000u | (uint32_t)n; /* NLOOP n, EOP */
    memcpy(&qw[0][0], &tag, 4);
    for (int k = 0; k < n; k++) {
        memcpy(qw[1 + k * 3], m->v[k], sizeof(m->v[k]));
        if (m->eye) {
            qw[1 + k * 3][2] = m->v[k][3]; /* (x, y, w, 1): the eye matrix */
            qw[1 + k * 3][3] = 1.0f;
        }
        qw[1 + k * 3 + 1][2] = 1.0f; /* Q */
        /* the strip flag: 0 where a strip starts */
        qw[1 + k * 3 + 1][3] = k == 0 || (m->split && k == m->split) ? 0.0f : 1.0f;
        for (int i = 0; i < 3; i++) {
            qw[1 + k * 3 + 2][i] = (float)m->c[i];
        }
        qw[1 + k * 3 + 2][3] = 127.0f;
    }
    const RdVuBatchDesc bd = {0, 0, 0};
    RdVuMeshDesc md;
    memset(&md, 0, sizeof(md));
    md.qw = (const float (*)[4])qw;
    md.qwCount = (uint32_t)(1 + n * 3);
    md.qwPerVertex = RD_VU_QW_PRELIT;
    md.batchCount = 1;
    md.batches = &bd;
    const RdMesh r = rd_create_vu_mesh(&md);
    CHECK(r.id != 0, "rd_create_vu_mesh (%s, %d vertices)", m->what, n);
    return r;
}

/* SCENE of a frame at native width W under cfg holding the first n
 * vertices of m as one code-32 strip (n 0: the clear only) */
static int renderStrip(const Cfg *cfg, int W, const StripMesh *m, int n, Img *scene)
{
    memset(scene, 0, sizeof(*scene));
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = cfg->preset;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.aspect = W == BAND ? 4.0f / 3.0f : (float)W / 384.0f;
    st.sceneScale = cfg->scale;
    if (!rd_init(BAND, GS_H, &st, NULL)) {
        return R_NO_DEVICE;
    }
    if (W != BAND) {
        rd_reset_scene((uint32_t)W, GS_H);
    }
    const RdMesh mesh = n ? stripMesh(m, n) : (RdMesh){0};
    rd_begin_frame();
    RdFrameHead h;
    memset(&h, 0, sizeof(h));
    memcpy(h.rgba, kBg, 4);
    h.gsW = (uint32_t)W;
    h.gsH = GS_H;
    h.clear = 1;
    rd_frame_head(&h);
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), (uint32_t)W, GS_H, 1);
    rd_test_gs(RD_TEST_Z_GEQUAL);
    rd_z_write(1);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(1);
    rd_texture_off();
    if (mesh.id && m->eye) {
        /* h = (x, y, Z w, w) from (x, y, w, 1) */
        RdVuDraw d;
        memset(&d, 0, sizeof(d));
        d.prog = RD_PROG_PRELIT;
        d.code = 32;
        d.clip = RD_VU_CLIP_REGION;
        d.vu.mem[16][0] = 1.0f;
        d.vu.mem[17][1] = 1.0f;
        d.vu.mem[18][2] = m->v[0][2];
        d.vu.mem[18][3] = 1.0f;
        d.vu.mem[23][3] = 1.0f;
        d.vu.mem[22][2] = 1.0f / 1020000.0f;
        rd_draw_vu_mesh(mesh, &d, RD_KEY(&kKey, 0, 0));
    } else if (mesh.id) {
        Tri t;
        memset(&t, 0, sizeof(t));
        t.code = 32;
        triDraw(mesh, &t, 0);
    }
    rd_end_frame(0);
    const bool ok = readImg(RD_TARGET_SCENE, scene);
    if (mesh.id) {
        rd_destroy_vu_mesh(mesh);
    }
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "strip rule, %s, W %d: %u validation errors", cfg->name, W, verr);
    rd_shutdown();
    return ok ? R_OK : R_FAIL;
}

static bool sameImg(const Img *a, const Img *b)
{
    return a->px && b->px && a->w == b->w && a->h == b->h &&
           memcmp(a->px, b->px, (size_t)a->w * a->h * 4) == 0;
}

/* whether SCENE shows something other than the clear at GS pixel (x, y) */
static bool drawnAt(const Img *im, int W, int x, int y)
{
    const uint32_t fx = texelsPerPixel(im, W), fy = im->h / GS_H;
    if (fx == 0 || fy == 0) {
        CHECK(0, "SCENE %ux%u is not a whole number of texels a GS pixel at W %d", im->w, im->h, W);
        return false;
    }
    const size_t tx = (size_t)(x - (2048 - W / 2)) * fx + fx / 2;
    const size_t ty = (size_t)(y - (2048 - GS_H / 2)) * fy + fy / 2;
    return memcmp(&im->px[(ty * im->w + tx) * 4], kBg, 3) != 0;
}

/* The region rule's strip test (case s): out of the box the PS2's picture;
 * in every other state a wall's failing triangle drawn clipped, a card and
 * a triangle with a vertex behind the eye nowhere. */
static void checkStripRule(void)
{
    static const struct {
        Cfg cfg;
        int W, extended;
    } kStates[] = {
        {{"Original 1x", RD_PRESET_ORIGINAL, 1.0f}, BAND, 0},
        {{"Enhanced 1x", RD_PRESET_ENHANCED, 1.0f}, BAND, 1},
        {{"Enhanced 1x", RD_PRESET_ENHANCED, 1.0f}, 688, 1},  /* 16:9 */
        {{"Enhanced 1x", RD_PRESET_ENHANCED, 1.0f}, 2048, 1}, /* 48:9 */
        {{"Enhanced 2x", RD_PRESET_ENHANCED, 2.0f}, BAND, 1},
        {{"Original 1x", RD_PRESET_ORIGINAL, 1.0f}, 688, 1}, /* a wide frame */
    };

    for (size_t i = 0; i < sizeof(kStates) / sizeof(kStates[0]); i++) {
        const Cfg *cfg = &kStates[i].cfg;
        const int W = kStates[i].W;
        Img none = {0}, wall3 = {0}, wall4 = {0}, card = {0}, eye3 = {0}, eye4 = {0};
        Img joined3 = {0}, joined7 = {0};
        if (renderStrip(cfg, W, &kWall, 0, &none) != R_OK ||
            renderStrip(cfg, W, &kWall, 3, &wall3) != R_OK ||
            renderStrip(cfg, W, &kWall, 4, &wall4) != R_OK ||
            renderStrip(cfg, W, &kCard, 4, &card) != R_OK ||
            renderStrip(cfg, W, &kEye, 3, &eye3) != R_OK ||
            renderStrip(cfg, W, &kEye, 4, &eye4) != R_OK ||
            renderStrip(cfg, W, &kJoined, 3, &joined3) != R_OK ||
            renderStrip(cfg, W, &kJoined, 7, &joined7) != R_OK) {
            CHECK(0, "strip rule, %s, W %d: a frame could not be rendered", cfg->name, W);
        } else {
            /* the passing triangle is drawn in every state */
            CHECK(drawnAt(&wall3, W, 2050, 2150),
                  "strip rule, %s, W %d: the wall's passing "
                  "triangle is drawn",
                  cfg->name, W);
            if (kStates[i].extended) {
                /* the failing triangle, clipped: inside the picture and up
                 * to its top row */
                CHECK(drawnAt(&wall4, W, 2160, 2100) && drawnAt(&wall4, W, 2140, 1830) &&
                          !drawnAt(&wall3, W, 2160, 2100),
                      "strip rule, %s, W %d: the wall's triangle with a vertex above the window "
                      "is drawn, clipped",
                      cfg->name, W);
            } else {
                CHECK(sameImg(&wall4, &wall3),
                      "strip rule, %s, W %d: out of the box the wall's triangle with a vertex "
                      "above the window is not drawn (the PS2's)",
                      cfg->name, W);
            }
            CHECK(sameImg(&card, &none) && !drawnAt(&card, W, 1860, 1950),
                  "strip rule, %s, W %d: the card is not drawn", cfg->name, W);
            /* the card's strip joined to a passing strip: a plank's face */
            if (kStates[i].extended) {
                CHECK(drawnAt(&joined7, W, 1860, 1950) && !drawnAt(&joined3, W, 1860, 1950),
                      "strip rule, %s, W %d: the joined card is drawn, clipped", cfg->name, W);
            } else {
                CHECK(sameImg(&joined7, &joined3),
                      "strip rule, %s, W %d: out of the box the joined card is not drawn",
                      cfg->name, W);
            }
            CHECK(sameImg(&eye4, &eye3),
                  "strip rule, %s, W %d: the triangle with a vertex behind the eye is not drawn",
                  cfg->name, W);
        }
        free(none.px);
        free(wall3.px);
        free(wall4.px);
        free(card.px);
        free(eye3.px);
        free(eye4.px);
        free(joined3.px);
        free(joined7.px);
    }
}

/* the texel the texel texture (s_grain 2) put at pixel (x, y) of im */
static void texelAt(const Img *im, uint32_t x, uint32_t y, int *u, int *v)
{
    const uint8_t *p = &im->px[((size_t)y * im->w + x) * 4];
    *u = p[0] | ((p[2] & 1) << 8);
    *v = p[1] | ((p[2] >> 1) << 8);
}

/* The grain's texels are the GS's (issue 75's centre rule at its root): the
 * sprite's s and t run 0..4.5 over its 512 GS pixels from GS (1792, 1792),
 * so the GS's pixel (x, y) of the 4:3 columns (sampled at its integer
 * coordinate) reads texel floor(4.5 x), floor(4.5 (y + 32)) mod 512 (the
 * frame's top row is GS y 1824), at every width.  Half the columns and
 * half the rows sit exactly on a texel boundary: there the GS takes the
 * texel the boundary starts.  Before the nearest filter was made the GS's
 * (common.hlsli gs_nearest_uv), the interpolated UV fell on either side
 * of the boundary by float rounding that depends on the pixel's place in
 * the target: about half the boundary pixels took the texel before, and a
 * different half at each width. */
static void checkGrainTexels(void)
{
    static const int widths[] = {512, 688, 1376, 2560};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        const int W = widths[i];
        Render g;
        s_grain = 2;
        const int st = renderAt(&kCfgs[0], W, &g);
        s_grain = 0;
        if (st != R_OK) {
            CHECK(0, "grain texels: the W %d frame could not be rendered", W);
            continue;
        }
        const uint32_t x0 = (uint32_t)(W / 2 - BAND / 2);
        size_t bad = 0;
        int shown = 0;
        for (uint32_t y = 0; texelsPerPixel(&g.scene, W) == 1 && y < g.scene.h; y++) {
            for (uint32_t x = 0; x < BAND; x++) {
                int u, v;
                texelAt(&g.scene, x0 + x, y, &u, &v);
                const int eu = (int)(9 * x / 2) % 512, ev = (int)(9 * (y + 32) / 2) % 512;
                if (u != eu || v != ev) {
                    if (shown++ < 4) {
                        printf("  grain W %d (%u,%u): texel %d,%d, the GS's %d,%d\n", W, x, y, u, v,
                               eu, ev);
                    }
                    bad++;
                }
            }
        }
        CHECK(texelsPerPixel(&g.scene, W) == 1 && bad == 0,
              "grain texels, W %d: %zu of the 4:3 columns' pixels read another texel than the GS",
              W, bad);
        freeRender(&g);
    }
}

/* the texel a pixel of s_tie's sprite read (its RGB); -1 when it is
 * another level's (the blue marker) */
static void tieDecode(const TieCase *tc, const uint8_t *p, int *u, int *v)
{
    static const uint8_t mark[3] = {0, 200, 100};
    if (tc->kind == 1) {
        const int b = p[2] >> 3;
        *u = (p[0] >> 3) | ((b & 1) << 5);
        *v = (p[1] >> 3) | ((b >> 1) << 5);
        return;
    }
    *u = p[0];
    *v = p[2] == mark[tc->lv] ? p[1] : -1;
}

/* The nearest filter at exact texel boundaries on every path that takes a
 * nearest texel (common.hlsli gs_nearest_texel), at W 512 and 688: the
 * sprite over the 4:3 square (GS 1792..2304 both ways) samples its pixel
 * (i, j) at the integer coordinate, U = 4.5 i, V = 4.5 j (the frame's row y
 * is j = y + 32), so every even column and row lands on a texel boundary,
 * where the GS takes the texel the boundary starts: at level l
 * floor(4.5 i / 2^l) mod the level's size.  The texture holds each texel's
 * coordinates.  The paths:
 *   sprite_ps, one level            gs_sample (gs_nearest_uv)
 *   sprite_texa_ps, MMAG linear,    gs_texa_texture's nearest branch
 *     MMIN nearest, minified
 *   GS levels, MMAG linear, MMIN 2  the integer filter's nearest taps
 *     (LCM 1, K 0.25 and 1)         (gs_texa_level), levels 0 and 1
 *   GS levels, both nearest,        gs_sample under DF_GS_LOD
 *     MMIN 2 and 3                  (gs_sample_lod_nearest)
 * Before the GS's rounding each took the texel before the boundary on some
 * of the boundary pixels (the interpolated UV's float rounding, which
 * depends on the pixel's place in the target). */
static void checkNearestTies(void)
{
    /* MXL 2, LCM 1; MMAG bit 5, MMIN bits 6..8, K bits 32..43 */
#define TIE_TEX1(mag, mmin, k)                                                                     \
    (1ull | (2ull << 2) | ((uint64_t)(mag) << 5) | ((uint64_t)(mmin) << 6) |                       \
     ((uint64_t)((k) & 0xFFF) << 32))
    static const TieCase cases[] = {
        {0, RD_FILTER_NEAREST, RD_FILTER_NEAREST, 0, 0, "sprite_ps, nearest, one level"},
        {1, RD_FILTER_LINEAR, RD_FILTER_NEAREST, 0, 0, "sprite_texa_ps, MMIN nearest"},
        {2, RD_FILTER_LINEAR, RD_FILTER_LINEAR, TIE_TEX1(1, 2, 4), 0,
         "GS levels, MMAG linear, MMIN 2, level 0"},
        {2, RD_FILTER_LINEAR, RD_FILTER_LINEAR, TIE_TEX1(1, 2, 16), 1,
         "GS levels, MMAG linear, MMIN 2, level 1"},
        {2, RD_FILTER_LINEAR, RD_FILTER_LINEAR, TIE_TEX1(0, 2, 4), 0,
         "GS levels, nearest, MMIN 2, level 0"},
        {2, RD_FILTER_LINEAR, RD_FILTER_LINEAR, TIE_TEX1(0, 2, 16), 1,
         "GS levels, nearest, MMIN 2, level 1"},
        {2, RD_FILTER_LINEAR, RD_FILTER_LINEAR, TIE_TEX1(0, 3, 16), 1,
         "GS levels, nearest, MMIN 3, LOD 1"},
    };
#undef TIE_TEX1
    static const int widths[] = {BAND, 688};
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]); wi++) {
            const int W = widths[wi];
            Render g;
            s_tie = &cases[c];
            const int st = renderAt(&kCfgs[0], W, &g);
            s_tie = NULL;
            if (st != R_OK) {
                CHECK(0, "ties, %s, W %d: the frame could not be rendered", cases[c].what, W);
                continue;
            }
            const int n = 64 >> cases[c].lv;
            const uint32_t x0 = (uint32_t)(W / 2 - BAND / 2);
            size_t bad = 0;
            for (uint32_t y = 0; texelsPerPixel(&g.scene, W) == 1 && y < g.scene.h; y++) {
                for (uint32_t i = 0; i < BAND; i++) {
                    int u, v;
                    tieDecode(&cases[c], &g.scene.px[((size_t)y * g.scene.w + x0 + i) * 4], &u, &v);
                    const int eu = (int)((9 * i / 2) >> cases[c].lv) % n;
                    const int ev = (int)((9 * (y + 32) / 2) >> cases[c].lv) % n;
                    if ((u != eu || v != ev) && bad++ < 3) {
                        printf("  ties, %s, W %d (%u,%u): texel %d,%d, the GS's %d,%d\n",
                               cases[c].what, W, i, y, u, v, eu, ev);
                    }
                }
            }
            CHECK(texelsPerPixel(&g.scene, W) == 1 && bad == 0,
                  "ties, %s, W %d: %zu pixels of the 4:3 columns read another texel than the GS",
                  cases[c].what, W, bad);
            freeRender(&g);
        }
    }
}

/* ------------------------------------- corners on pixel boundaries (p) */

/* Code-32 triangles inside the 4:3 band (identity matrix of triDraw: GS
 * pixels as given), one flat colour each: corners on pixel centres (GS
 * integers) with edges through a run of pixel centres (slopes 1, 1/4, 0
 * and vertical), corners on pixel corners (GS x.5: the 12.4 value 8) with
 * edges along pixel boundaries, and a mix.  Every pixel centre on an edge
 * is a tie the GPU's fill rule decides, so a corner moved by the least
 * rounding changes the coverage. */
static const Tri kCorners[] = {
    {"centres, slope 1",
     32,
     {{1900, 1900, 1000000, 1}, {2000, 2000, 1000000, 1}, {1900, 2100, 1000000, 1}},
     {{220, 60, 60}, {220, 60, 60}, {220, 60, 60}}},
    {"pixel corners, edges on boundaries",
     32,
     {{2000.5f, 1900.5f, 1000000, 1},
      {2100.5f, 1900.5f, 1000000, 1},
      {2000.5f, 2000.5f, 1000000, 1}},
     {{60, 220, 60}, {60, 220, 60}, {60, 220, 60}}},
    {"centres, slope 1/4",
     32,
     {{2150, 1950, 1000000, 1}, {2250, 1975, 1000000, 1}, {2200, 2100, 1000000, 1}},
     {{60, 60, 220}, {60, 60, 220}, {60, 60, 220}}},
    {"mixed",
     32,
     {{1950, 2150, 1000000, 1}, {2150.5f, 2180, 1000000, 1}, {2050, 2250.5f, 1000000, 1}},
     {{220, 220, 60}, {220, 220, 60}, {220, 220, 60}}},
    {"left half of the band, corners at its edge",
     32,
     {{1792.5f, 1830, 1000000, 1}, {1880, 1830, 1000000, 1}, {1792.5f, 1880.5f, 1000000, 1}},
     {{220, 60, 220}, {220, 60, 220}, {220, 60, 220}}},
    /* a corner on a pixel corner inside the band, the others beside the
     * 4:3 frame (beside the target at W 512, inside it at a wider W): the
     * slope 1/3 edge from the corner runs through every third pixel centre
     * (1899 - 3k, 2121 + k), (2262 + 3k, 1841 + k) and across the band
     * edge, where the GPU clipped it at the target's edge at W 512 only */
    {"across the left band edge",
     32,
     {{1900.5f, 2120.5f, 1000000, 1},
      {1600.5f, 2220.5f, 1000000, 1},
      {1600.5f, 2120.5f, 1000000, 1}},
     {{60, 220, 220}, {60, 220, 220}, {60, 220, 220}}},
    {"across the right band edge",
     32,
     {{2260.5f, 1840.5f, 1000000, 1},
      {2460.5f, 1840.5f, 1000000, 1},
      {2410.5f, 1890.5f, 1000000, 1}},
     {{240, 160, 60}, {240, 160, 60}, {240, 160, 60}}},
};
#define NCORNERS ((int)(sizeof(kCorners) / sizeof(kCorners[0])))

/* SCENE of a frame at native width W under cfg holding kCorners, flat */
static int renderCorners(const Cfg *cfg, int W, Img *scene)
{
    memset(scene, 0, sizeof(*scene));
    RdSettings st;
    memset(&st, 0, sizeof(st));
    st.preset = cfg->preset;
    st.outputWidth = 640;
    st.outputHeight = 480;
    st.aspect = W == BAND ? 4.0f / 3.0f : (float)W / 384.0f;
    st.sceneScale = cfg->scale;
    if (!rd_init(BAND, GS_H, &st, NULL)) {
        return R_NO_DEVICE;
    }
    if (W != BAND) {
        rd_reset_scene((uint32_t)W, GS_H);
    }
    RdMesh meshes[NCORNERS];
    for (int i = 0; i < NCORNERS; i++) {
        meshes[i] = triMesh(&kCorners[i]);
    }
    rd_begin_frame();
    RdFrameHead h;
    memset(&h, 0, sizeof(h));
    memcpy(h.rgba, kBg, 4);
    h.gsW = (uint32_t)W;
    h.gsH = GS_H;
    h.clear = 1;
    rd_frame_head(&h);
    rd_select_list(0);
    rd_set_target(rd_target(RD_TARGET_SCENE), rd_target(RD_TARGET_SCENE), (uint32_t)W, GS_H, 1);
    rd_test_gs(RD_TEST_Z_ALWAYS);
    rd_z_write(0);
    rd_blend(RD_BLEND_LERP_AS, 0x80, 0);
    rd_abe(0);
    rd_pabe(0);
    rd_fba(0);
    rd_gouraud(0);
    rd_texture_off();
    for (int i = 0; i < NCORNERS; i++) {
        triDraw(meshes[i], &kCorners[i], i);
    }
    /* a Gouraud screen-prim triangle (the CPU GIF path, sprite.hlsl): its
       colours from the pixel's offset to its corner, the same at every
       width (rd_screen_planes.c) */
    static const uint8_t g0[4] = {230, 40, 40, 0x80}, g1[4] = {40, 230, 40, 0x80},
                         g2[4] = {40, 40, 230, 0x80};
    RdScreenVtx gv[3] = {vtx(2010 * 16, 2010 * 16, g0, 0, 0), vtx(2140 * 16, 2030 * 16, g1, 0, 0),
                         vtx(2040 * 16, 2140 * 16, g2, 0, 0)};
    rd_gouraud(1);
    rd_screen_prims(RD_PRIM_TRIANGLES, gv, 3, RD_SPACE_WORLD, 1, 0);
    rd_end_frame(0);
    const bool ok = readImg(RD_TARGET_SCENE, scene);
    for (int i = 0; i < NCORNERS; i++) {
        rd_destroy_vu_mesh(meshes[i]);
    }
    const uint32_t verr = rhi_vk_validation_error_count();
    CHECK(verr == 0, "corners, %s, W %d: %u validation errors", cfg->name, W, verr);
    rd_shutdown();
    return ok ? R_OK : R_FAIL;
}

/* Case p: the triangles' coverage in the 4:3 columns is the W = 512 one at
 * every width, in every preset */
static void checkBoundaryCorners(void)
{
    for (int c = 0; c < NCFGS; c++) {
        Img ref;
        if (renderCorners(&kCfgs[c], BAND, &ref) != R_OK) {
            CHECK(0, "corners, %s: the W 512 frame could not be rendered", kCfgs[c].name);
            continue;
        }
        static const int at[NCORNERS][2] = {{1910, 1950}, {2030, 1930}, {2200, 2000}, {2050, 2190},
                                            {1800, 1835}, {1800, 2140}, {2290, 1845}};
        for (int t = 0; t < NCORNERS; t++) {
            CHECK(drawnAt(&ref, BAND, at[t][0], at[t][1]), "corners, %s: \"%s\" is drawn at W 512",
                  kCfgs[c].name, kCorners[t].what);
        }
        CHECK(drawnAt(&ref, BAND, 2060, 2060), "corners, %s: the Gouraud screen prim is drawn",
              kCfgs[c].name);
        for (int i = 1; i < NWIDTHS; i++) {
            Img im;
            if (renderCorners(&kCfgs[c], kWidths[i], &im) != R_OK) {
                CHECK(0, "corners, %s, W %d: the frame could not be rendered", kCfgs[c].name,
                      kWidths[i]);
                continue;
            }
            compareCentre("corners SCENE", &kCfgs[c], kWidths[i], &im, &ref, 0);
            free(im.px);
        }
        free(ref.px);
    }
}

int main(void)
{
    rd__set_not_implemented_fatal(true);
    static Render ref[NCFGS];

    /* W = 512: the references, the Original hash, the frame is visible */
    for (int c = 0; c < NCFGS; c++) {
        const int st = renderAt(&kCfgs[c], BAND, &ref[c]);
        if (st == R_NO_DEVICE) {
            printf("SKIP rd_native_test: no usable Vulkan device\n");
            return 77;
        }
        if (st != R_OK) {
            continue;
        }
        checkShows("SCENE", &ref[c].scene, BAND, 0);
        checkShows("DISPLAY", &ref[c].display, BAND, 2);
        if (c == 0) {
            const uint64_t hs = fnv(ref[c].scene.px, (size_t)ref[c].scene.w * ref[c].scene.h * 4);
            const uint64_t hd =
                fnv(ref[c].display.px, (size_t)ref[c].display.w * ref[c].display.h * 4);
            printf("hash %s W 512: SCENE %016llx DISPLAY %016llx\n", kCfgs[c].name,
                   (unsigned long long)hs, (unsigned long long)hd);
            if (kExpect[0]) {
                CHECK(hs == kExpect[0], "Original W 512 SCENE hash %016llx, expected %016llx",
                      (unsigned long long)hs, (unsigned long long)kExpect[0]);
            }
            if (kExpect[1]) {
                CHECK(hd == kExpect[1], "Original W 512 DISPLAY hash %016llx, expected %016llx",
                      (unsigned long long)hd, (unsigned long long)kExpect[1]);
            }
        }
    }

    checkFilmGrain();
    checkStripRule();
    checkGrainTexels();
    checkNearestTies();
    checkBoundaryCorners();
    /* the wide frames, every preset (Original at a wide aspect is the 4:3
       picture with the sides): the centre rule against W 512 (c), the
       nesting in the widest (n), the band edges (e) */
    const int wideW = kWidths[NWIDTHS - 1];
    for (int c = 0; c < NCFGS; c++) {
        if (!ref[c].scene.px) {
            continue;
        }
        Render wide;
        if (renderAt(&kCfgs[c], wideW, &wide) != R_OK) {
            CHECK(0, "%s, W %d: the frame could not be rendered", kCfgs[c].name, wideW);
            continue;
        }
        compareNested("SCENE", &kCfgs[c], BAND, &ref[c].scene, wideW, &wide.scene, 0);
        compareNested("DISPLAY", &kCfgs[c], BAND, &ref[c].display, wideW, &wide.display, 2);
        for (int i = 1; i < NWIDTHS; i++) {
            const int W = kWidths[i];
            Render r;
            if (W == wideW) {
                r = wide;
            } else if (renderAt(&kCfgs[c], W, &r) != R_OK) {
                CHECK(0, "%s, W %d: the frame could not be rendered", kCfgs[c].name, W);
                continue;
            }
            compareCentre("SCENE", &kCfgs[c], W, &r.scene, &ref[c].scene, 0);
            compareCentre("DISPLAY", &kCfgs[c], W, &r.display, &ref[c].display, 2);
            if (W != wideW) {
                compareNested("SCENE", &kCfgs[c], W, &r.scene, wideW, &wide.scene, 0);
                compareNested("DISPLAY", &kCfgs[c], W, &r.display, wideW, &wide.display, 2);
                freeRender(&r);
            }
            /* the band edges, on the frame without the 4:3 UI sprite */
            Render nu;
            s_noUi = 1;
            const int st = renderAt(&kCfgs[c], W, &nu);
            s_noUi = 0;
            if (st != R_OK) {
                CHECK(0, "%s, W %d: the frame without the UI could not be rendered", kCfgs[c].name,
                      W);
                continue;
            }
            checkContinuity(&kCfgs[c], W, &nu.scene);
            freeRender(&nu);
            printf("  %s, W %d done\n", kCfgs[c].name, W);
        }
        freeRender(&wide);
    }
    for (int c = 0; c < NCFGS; c++) {
        freeRender(&ref[c]);
    }
    if (failures) {
        printf("rd_native_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_native_test: ok\n");
    return 0;
}
