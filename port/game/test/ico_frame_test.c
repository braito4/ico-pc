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
 *   options ico_video_frame: the defaults (4:3) 512, aspect 16:9 688, 48:9
 *           2048; ico_video_wide_x is the width over 512
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

static void checkOptions(void)
{
    IcoVideoOptions o;
    IcoFrame f;
    ico_video_defaults(&o);
    ico_video_set(&o);
    ico_video_frame(&f, 448);
    CHECK(f.w == 512 && f.h == 448 && f.aspect == 4.0f / 3.0f && ico_video_wide_x() == 1.0f,
          "options: the defaults are the 4:3 frame (%d, %g)", f.w, (double)f.aspect);
    o.aspect = ICO_ASPECT_16_9;
    ico_video_set(&o);
    ico_video_frame(&f, 448);
    CHECK(f.w == 688 && ico_video_wide_x() == 688.0f / 512.0f, "options: 16:9 is 688 wide (%d, %g)",
          f.w, (double)ico_video_wide_x());
    o.aspect = ICO_ASPECT_48_9;
    ico_video_set(&o);
    ico_video_frame(&f, 512);
    CHECK(f.w == 2048 && f.h == 512 && ico_video_wide_x() == 4.0f,
          "options: 48:9 is 2048 wide (%d x %d)", f.w, f.h);
    ico_video_defaults(&o);
    ico_video_set(&o);
}

int main(void)
{
    checkWidth();
    checkMake();
    checkX();
    checkTw();
    checkOptions();
    if (failures) {
        printf("ico_frame_test: %d failures\n", failures);
        return 1;
    }
    printf("ico_frame_test: ok\n");
    return 0;
}
