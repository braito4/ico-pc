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
 *   - a full-frame tagged flat sprite (RD_SPACE_FULLSCREEN), blended;
 *   - a second flat full-frame sprite standing in for the fog pass (the
 *     real RD_POST_FOG needs the depth LUT of rd_fog_test; the fog's 4:3
 *     rule is that test's and W2-SPACES' business);
 *   - the DISPLAY reduction (RD_POST_REDUCTION) at the end of list 12.
 *
 * The cases, by the wave that makes each pass:
 *   c  every W, every preset: SCENE and DISPLAY columns
 *      [(W/2 - 256) s, +512 s) equal the W = 512 render's byte for byte
 *      (Gate B's centre rule).  DISPLAY leaves out two GS pixels at each
 *      end of the 512 columns: the reduction crops its picture 2 pixels
 *      from the frame's edge and its bilinear reads one pixel past, which
 *      at W = 512 is the frame's edge and at a wider W is picture.
 *      Passes after W2-SPACES (the space rewrite: no squeeze, no wideX) and
 *      W2-VU (the drop rule).
 *   e  every W > 512, Enhanced: no step of SCENE across either old band edge
 *      (the column pair at (W/2 -+ 256) s) is larger than the 99th
 *      percentile (+1 LSB for rounding) of the horizontal gradient of its
 *      row within 16 GS pixels of the edge, per channel.  Passes after
 *      W2-SPACES.
 *   h  W = 512, Original 1x: the hash of SCENE and of DISPLAY equal
 *      kExpect (Gate A in miniature: the Original picture at 4:3 does not
 *      change).  Runs always; kExpect is zero until the first run's printed
 *      hashes are filled in (then the check is off).
 *   The W = 512 renders also have to show the frame (not the clear colour).
 * The wide cases (c, e) run only with ICO_NATIVE_GATE=1 in the environment;
 * without it the test checks W = 512 and exits 77 (skipped), so the ctest is
 * green until W2 and becomes the gate once the variable is set in the ctest.
 *
 * Exit 0 all, 1 a mismatch, 77 without a Vulkan device or without the gate. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_mesh.h"
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
 * of SCENE's and of DISPLAY's readback bytes.  TODO(orchestrator): fill both
 * from the "hash" line of the first run; while zero the check is off. */
static const uint64_t kExpect[2] = {0, 0};

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
        }
    }
    return rd_create_texture(512, 512, px, RD_TEXA_80_80, "rd_native");
}

/* ----------------------------------------------------------- the frame */

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
    spriteAbs(bl, top, br, bot, grey, RD_SPACE_UI, 8, 8, BAND * 16 + 8, GS_H * 16 + 8);

    /* the full-frame items: a tint, and the fog stand-in */
    static const uint8_t tint[4] = {30, 60, 120, 0x28}, fog[4] = {0xA0, 0xB0, 0xC0, 0x18};
    rd_texture_off();
    spriteAbs(l, top, r, bot, tint, RD_SPACE_FULLSCREEN, 0, 0, 0, 0);
    spriteAbs(l, top, r, bot, fog, RD_SPACE_FULLSCREEN, 0, 0, 0, 0);

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
    drawFrame(W, tex, meshes);
    const bool ok =
        readImg(RD_TARGET_SCENE, &out->scene) && readImg(RD_TARGET_DISPLAY, &out->display);
    for (int i = 0; i < NTRIS; i++) {
        rd_destroy_vu_mesh(meshes[i]);
    }
    rd_destroy_texture(tex);
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
 * the row's horizontal gradient within 16 GS pixels of the edge */
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
                if (step > p99 + 1) {
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

int main(void)
{
    const char *gateEnv = getenv("ICO_NATIVE_GATE");
    const int gate = gateEnv && strcmp(gateEnv, "1") == 0;
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

    if (gate) {
        /* the wide frames, Enhanced only (Original is the 4:3 picture) */
        for (int c = 1; c < NCFGS; c++) {
            if (!ref[c].scene.px) {
                continue;
            }
            for (int i = 1; i < NWIDTHS; i++) {
                Render r;
                const int st = renderAt(&kCfgs[c], kWidths[i], &r);
                if (st != R_OK) {
                    CHECK(0, "%s, W %d: the frame could not be rendered", kCfgs[c].name,
                          kWidths[i]);
                    continue;
                }
                compareCentre("SCENE", &kCfgs[c], kWidths[i], &r.scene, &ref[c].scene, 0);
                compareCentre("DISPLAY", &kCfgs[c], kWidths[i], &r.display, &ref[c].display, 2);
                checkContinuity(&kCfgs[c], kWidths[i], &r.scene);
                printf("  %s, W %d done\n", kCfgs[c].name, kWidths[i]);
                freeRender(&r);
            }
        }
    }
    for (int c = 0; c < NCFGS; c++) {
        freeRender(&ref[c]);
    }
    if (failures) {
        printf("rd_native_test: %d failures\n", failures);
        return 1;
    }
    if (!gate) {
        printf("SKIP rd_native_test: W 512 ok; the wide cases need ICO_NATIVE_GATE=1\n");
        return 77;
    }
    printf("rd_native_test: ok\n");
    return 0;
}
