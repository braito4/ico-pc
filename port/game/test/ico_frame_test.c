/* ico_frame_test.c: the native frame (port/include/ico_frame.h) and the
 * options' frame (port/game/video_options.c ico_video_frame).  CPU only.
 *
 *   width   ico_frame_width_for: 4:3 is 512 exactly, 16:10 624, 16:9 688,
 *           21:9 912, 32:9 1376, 48:9 2048, 60:9 2560 (the widest, also past
 *           it); narrower than 4:3 and NaN 512; the 1e-5 edge (k 1 + 0.5e-5
 *           is 512, 1 + 2e-5 is 528); over the whole range a multiple of 16,
 *           never narrower than 512 k, less than 16 wider, never shrinking
 *           as the aspect grows
 *   make    ico_frame_make: 4:3 is 512 x h, FBW 8, wExact 512, aspect 4/3
 *           exactly; 16:9 FBW 11, wExact 512 x 4/3, aspect 16/9
 *   x       ico_frame_x: v itself at 512; v * 688 / 512 towards zero at 16:9
 *   tw      ico_frame_tw: ceil(log2 n), 0 for n <= 1
 *   tex0    ico_frame_tex0_w at 4:3 rebuilds every literal TEX0 word the
 *           post passes, the shadow levels and the reflection blocks wrote
 *           (staticBlur.c, Shadow.c, puddle.c, pool.c, queen_barrier_disp.c,
 *           GsBase.c), each as its call site spells it now; at 16:9 and
 *           48:9 the TBW and TW of W / 2, W / 4 and W
 *   options ico_video_frame: the defaults (4:3) 512, aspect 16:9 688, 48:9
 *           2048
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "video_options.h"

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

static void checkWidth(void)
{
    static const struct {
        float aspect;
        int w;
        const char *name;
    } cases[] = {
        {4.0f / 3.0f, 512, "4:3"},
        {16.0f / 10.0f, 624, "16:10"},
        {16.0f / 9.0f, 688, "16:9"},
        {64.0f / 27.0f, 912, "21:9"},
        {32.0f / 9.0f, 1376, "32:9"},
        {48.0f / 9.0f, 2048, "48:9"},
        {60.0f / 9.0f, 2560, "60:9"},
        {ICO_FRAME_ASPECT_MAX, 2560, "the widest"},
        {100.0f, ICO_FRAME_W_MAX, "past the widest"},
        {1.0f, 512, "1:1"},
        {0.0f, 512, "0"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int w = ico_frame_width_for(cases[i].aspect);
        CHECK(w == cases[i].w, "width %s: %d, want %d", cases[i].name, w, cases[i].w);
    }
    CHECK(ico_frame_width_for(nanf("")) == 512, "width NaN: 512");
    CHECK(ICO_FRAME_W_MAX == ICO_FRAME_BASE_W * ICO_FRAME_K_MAX, "W_MAX is 512 K_MAX");
    /* the 1e-5 edge */
    const float in = 4.0f / 3.0f * (1.0f + 0.5e-5f), out = 4.0f / 3.0f * (1.0f + 2e-5f);
    CHECK(ico_frame_width_for(in) == 512, "edge: k 1 + 0.5e-5 is 512 (%d)",
          ico_frame_width_for(in));
    CHECK(ico_frame_width_for(out) == 528, "edge: k 1 + 2e-5 is 528 (%d)",
          ico_frame_width_for(out));
    /* the whole range */
    int last = 0, bad = 0;
    for (int i = 0; i <= 20000; i++) {
        const float a = 4.0f / 3.0f + (ICO_FRAME_ASPECT_MAX - 4.0f / 3.0f) * (float)i / 20000.0f;
        const int w = ico_frame_width_for(a);
        const double k = (double)a * 0.75 > 5.0 ? 5.0 : (double)a * 0.75;
        if (w % 16 != 0 || w < 512 || w > ICO_FRAME_W_MAX || (double)w < 512.0 * k - 0.01 ||
            (double)w - 512.0 * k >= 16.0 || w < last) {
            if (bad++ < 4) {
                printf("  width: aspect %.7f -> %d (512 k %.4f, before %d)\n", (double)a, w,
                       512.0 * k, last);
            }
        }
        last = w;
    }
    CHECK(bad == 0, "width: %d aspects off the rule", bad);
}

static void checkMake(void)
{
    IcoFrame f;
    ico_frame_make(&f, 4.0f / 3.0f, 448);
    CHECK(f.w == 512 && f.h == 448 && f.fbw == 8 && f.wExact == 512.0f && f.aspect == 4.0f / 3.0f,
          "make 4:3: %d x %d, FBW %d, wExact %g, aspect %g", f.w, f.h, f.fbw, (double)f.wExact,
          (double)f.aspect);
    ico_frame_make(&f, 4.0f / 3.0f * (1.0f + 0.5e-5f), 512);
    CHECK(f.w == 512 && f.h == 512 && f.wExact == 512.0f && f.aspect == 4.0f / 3.0f,
          "make at the edge: the 4:3 frame (%d, %g, %g)", f.w, (double)f.wExact, (double)f.aspect);
    ico_frame_make(&f, 16.0f / 9.0f, 448);
    CHECK(f.w == 688 && f.fbw == 11 && fabsf(f.wExact - 512.0f * 4.0f / 3.0f) < 1e-3f &&
              f.aspect == 16.0f / 9.0f,
          "make 16:9: %d, FBW %d, wExact %g, aspect %g", f.w, f.fbw, (double)f.wExact,
          (double)f.aspect);
    ico_frame_make(&f, 100.0f, 448);
    CHECK(f.w == 2560 && f.fbw == 40 && f.wExact == 2560.0f && f.aspect == ICO_FRAME_ASPECT_MAX,
          "make past the widest: %d, FBW %d, wExact %g, aspect %g", f.w, f.fbw, (double)f.wExact,
          (double)f.aspect);
}

static void checkX(void)
{
    IcoFrame f;
    static const int vs[] = {-300, -1, 0, 1, 127, 128, 255, 256, 511, 512, 4096};
    ico_frame_make(&f, 4.0f / 3.0f, 448);
    for (size_t i = 0; i < sizeof(vs) / sizeof(vs[0]); i++) {
        CHECK(ico_frame_x(&f, vs[i]) == vs[i], "x 4:3: %d -> %d", vs[i], ico_frame_x(&f, vs[i]));
    }
    ico_frame_make(&f, 16.0f / 9.0f, 448);
    static const int want[][2] = {{256, 344}, {512, 688},   {128, 172}, {1, 1},
                                  {3, 4},     {-256, -344}, {-3, -4},   {0, 0}};
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK(ico_frame_x(&f, want[i][0]) == want[i][1], "x 16:9: %d -> %d, want %d", want[i][0],
              ico_frame_x(&f, want[i][0]), want[i][1]);
    }
}

static void checkTw(void)
{
    static const int want[][2] = {{0, 0},     {1, 0},     {2, 1},    {3, 2},    {128, 7},
                                  {256, 8},   {257, 9},   {512, 9},  {688, 10}, {1376, 11},
                                  {2048, 11}, {2560, 12}, {4096, 12}};
    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        CHECK(ico_frame_tw(want[i][0]) == want[i][1], "tw %d: %d, want %d", want[i][0],
              ico_frame_tw(want[i][0]), want[i][1]);
    }
}

/* the TEX0 word of a buffer the 4:3 width v at this frame, ORed with the
   rest of the literal (TBP, TH, TCC, TFX, PSM), as the call sites build it */
static long long w0(const IcoFrame *f, int v, long long rest)
{
    return ico_frame_tex0_w(f, v) | rest;
}

static void checkTex0(void)
{
    IcoFrame f;
    ico_frame_make(&f, 4.0f / 3.0f, 448);

    static const struct {
        int v;
        long long rest, literal;
        const char *where;
    } kWords[] = {
        /* staticBlur.c */
        {256, 0x15C0000000LL, 0x20010000LL | 0x15C0000000LL, "blurPass, depthFieldPass"},
        {256, 0x5C0000000LL, 0x20010000LL | 0x5C0000000LL, "Work0/Work1/Work3 reads"},
        {256, 0x1600000000LL, 0x20010000LL | 0x1600000000LL, "copyToWork2"},
        {128, 0x5C0000000LL, 0x1C008000LL | 0x5C0000000LL, "blendWork0ToWork1"},
        {128, 0x3F00LL | 0x5C0000000LL, 0x5DC00BF00LL, "pasteFeedBackAreaToFB, addTap"},
        {512, 0x640000000LL, (512LL / 64 << 14) | 0x664000000LL, "frame-sized reads"},
        {512, 0x640000800LL, (512LL / 64 << 14) | 0x664000800LL, "SCENE reads"},
        {512, 0x640100000LL, (512LL / 64 << 14) | 0x664100000LL, "MotionBlur's DISPLAY"},
        /* Shadow.c, level i at TEX0 TBW (512 >> i) / 64, TW 9 - i */
        {512, 0, (8LL << 14) | (9LL << 26), "shadow level 0 (the count)"},
        {256, 0, (4LL << 14) | (8LL << 26), "shadow level 1"},
        {128, 0, (2LL << 14) | (7LL << 26), "shadow level 2"},
        {64, 0, (1LL << 14) | (6LL << 26), "shadow level 3"},
        /* puddle.c, pool.c: the reflection block */
        {256, 0x600000000LL, 0x20010000LL | 0x600000000LL, "puddle/pool block"},
        /* queen_barrier_disp.c */
        {512, 0x600000000LL, 0x24020000LL | 0x600000000LL, "barrier block"},
        {512, 0x640000800LL, (8LL << 14) | 0x664000800LL, "barrier/pool scene copy"},
        /* GsBase.c */
        {512, 0x640000800LL, (8LL << 14) | 0x664000800LL, "gsb_Reduction"},
        {512, 0x600100000LL, (8LL << 14) | (0xC482LL << 19), "gsb_KeepFrameBuffer"},
    };

    for (size_t i = 0; i < sizeof(kWords) / sizeof(kWords[0]); i++) {
        const long long got = w0(&f, kWords[i].v, kWords[i].rest);
        CHECK(got == kWords[i].literal, "tex0 4:3 %s: 0x%llx, the literal 0x%llx", kWords[i].where,
              (unsigned long long)got, (unsigned long long)kWords[i].literal);
    }

    /* 16:9 (688): W / 2 344 is TBW 6 TW 9, W / 4 172 TBW 3 TW 8, W TBW 11
       TW 10; 48:9 (2048): 1024 TBW 16 TW 10, 512 TBW 8 TW 9, 2048 TBW 32
       TW 11 */
    static const struct {
        float aspect;
        int v;
        int tbw, tw;
    } kWide[] = {{16.0f / 9.0f, 256, 6, 9},   {16.0f / 9.0f, 128, 3, 8},
                 {16.0f / 9.0f, 512, 11, 10}, {16.0f / 3.0f, 256, 16, 10},
                 {16.0f / 3.0f, 128, 8, 9},   {16.0f / 3.0f, 512, 32, 11},
                 {16.0f / 3.0f, 64, 4, 8}};

    for (size_t i = 0; i < sizeof(kWide) / sizeof(kWide[0]); i++) {
        ico_frame_make(&f, kWide[i].aspect, 448);
        const long long got = ico_frame_tex0_w(&f, kWide[i].v);
        const long long want = ((long long)kWide[i].tbw << 14) | ((long long)kWide[i].tw << 26);
        CHECK(got == want, "tex0 W %d, v %d: 0x%llx, want TBW %d TW %d", f.w, kWide[i].v,
              (unsigned long long)got, kWide[i].tbw, kWide[i].tw);
    }
}

static void checkOptions(void)
{
    IcoVideoOptions o;
    IcoFrame f;
    ico_video_defaults(&o);
    ico_video_set(&o);
    ico_video_frame(&f, 448);
    CHECK(f.w == 512 && f.h == 448 && f.aspect == 4.0f / 3.0f,
          "options: the defaults are the 4:3 frame (%d, %g)", f.w, (double)f.aspect);
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    ico_video_frame(&f, 448);
    CHECK(f.w == 688, "options: 16:9 is 688 wide (%d)", f.w);
    o.aspect = ICO_ASPECT_48_9;
    ico_video_set(&o);
    ico_video_frame(&f, 512);
    CHECK(f.w == 2048 && f.h == 512, "options: 48:9 is 2048 wide (%d x %d)", f.w, f.h);
    ico_video_defaults(&o);
    ico_video_set(&o);
}

int main(void)
{
    checkWidth();
    checkMake();
    checkX();
    checkTw();
    checkTex0();
    checkOptions();
    if (failures) {
        printf("ico_frame_test: %d failures\n", failures);
        return 1;
    }
    printf("ico_frame_test: ok\n");
    return 0;
}
