/*
 * port/game/test/demo_walk_test.c
 *
 * The opening's soldiers walk off the wide picture (issue 68,
 * port/game/demo_walk.c), over synthetic keys (no disc):
 *   - the plan: a walk whose root stops at frame 130 while the legs walk in
 *     place to frame 170, then every key holds (cut 23's D1_C23_SA shape):
 *     the tail, the stop, the 40-frame stride and the velocity; a walk keyed
 *     to the end (D1_C18's shape) stops at the last frame; standing, rising
 *     and too-short motions have no plan;
 *   - the decision: at gsFrame.w 512 the extension never starts, for any
 *     root position, depth or frame; at 2048 it starts only outside the 4:3
 *     picture and inside the native frame, and holds once the root is
 *     ICO_DEMO_WALK_MARGIN past the native frame's side or behind the camera;
 *   - the walk: the cycle frame and the root e frames past the stop, a
 *     declined actor staying declined, a replay from before the stop
 *     starting over, the cap, and the hook's listed motions.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "demo_walk.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define NEAR(a, b, e) (fabsf((float)(a) - (float)(b)) <= (e))

#define TWO_PI 6.2831853f

/* a synthetic motion: the root moves rootStep a frame up to rootStop, the
   node keys swing with period `period` up to poseStop, then hold */
typedef struct Synth {
    int frames;
    int nodes;
    int period;
    int rootStop;
    int poseStop;
    float rootStep[3];
} Synth;

static void synthRoot(const void *data, int frame, float *out3)
{
    const Synth *s = data;
    int f = frame < s->rootStop ? frame : s->rootStop;

    out3[0] = 100.0f + s->rootStep[0] * (float)f;
    out3[1] = -50.0f + s->rootStep[1] * (float)f;
    out3[2] = 20.0f + s->rootStep[2] * (float)f;
}

static void synthPose(const void *data, int frame, int node, float *out3)
{
    const Synth *s = data;
    int f = frame < s->poseStop ? frame : s->poseStop - 1;
    int c;

    for (c = 0; c < 3; c++) {
        float ph = (float)node * 0.7f + (float)c * 1.3f;

        out3[c] = s->period > 0 ? 0.3f * sinf(TWO_PI * (float)f / (float)s->period + ph) : 0.1f;
    }
}

static void testPlan(void)
{
    IcoDemoWalkPlan p;
    Synth sa = {250, 4, 40, 130, 170, {-2.0f, 0.0f, 2.0f}};
    Synth end = {210, 4, 42, 1000, 1000, {-1.7f, 0.0f, 0.0f}};
    Synth stand = {200, 4, 40, 0, 1000, {0.0f, 0.0f, 0.0f}};
    Synth rise = {360, 4, 40, 1000, 1000, {0.0f, 22.0f, 0.0f}};
    Synth slow = {200, 4, 40, 1000, 1000, {0.5f, 0.0f, 0.5f}};
    Synth shortWalk = {60, 4, 40, 1000, 1000, {-2.0f, 0.0f, 2.0f}};

    /* cut 23's shape: root stops at 130, legs in place to 170, then held */
    CHECK(ico_demo_walk_analyse(&p, sa.frames, sa.nodes, &sa, synthRoot, synthPose) == 1);
    CHECK(p.valid == 1);
    CHECK(p.tail >= 169 && p.tail <= 170);
    CHECK(p.stop == 130);
    CHECK(p.period == 40);
    CHECK(NEAR(p.vel[0], -2.0f, 1e-3f) && NEAR(p.vel[1], 0.0f, 0.0f) &&
          NEAR(p.vel[2], 2.0f, 1e-3f));
    CHECK(NEAR(p.root[0], 100.0f - 260.0f, 1e-2f) && NEAR(p.root[2], 20.0f + 260.0f, 1e-2f));

    /* a walk keyed to its last frame: the stop is the end hold */
    CHECK(ico_demo_walk_analyse(&p, end.frames, end.nodes, &end, synthRoot, synthPose) == 1);
    CHECK(p.tail == end.frames - 1);
    CHECK(p.stop == end.frames - 1);
    CHECK(p.period == 42);
    CHECK(NEAR(p.vel[0], -1.7f, 1e-3f));

    /* no walk: standing, rising on a platform (y only), too slow */
    CHECK(ico_demo_walk_analyse(&p, stand.frames, stand.nodes, &stand, synthRoot, synthPose) == 0);
    CHECK(p.valid == 0);
    CHECK(ico_demo_walk_analyse(&p, rise.frames, rise.nodes, &rise, synthRoot, synthPose) == 0);
    CHECK(ico_demo_walk_analyse(&p, slow.frames, slow.nodes, &slow, synthRoot, synthPose) == 0);
    /* too short for two strides before the stop */
    CHECK(ico_demo_walk_analyse(&p, shortWalk.frames, shortWalk.nodes, &shortWalk, synthRoot,
                                synthPose) == 0);
    /* degenerate input */
    CHECK(ico_demo_walk_analyse(&p, 2, 4, &sa, synthRoot, synthPose) == 0);
    CHECK(ico_demo_walk_analyse(&p, 250, 0, &sa, synthRoot, synthPose) == 0);
}

static void testDecision(void)
{
    int dx;
    int dy;
    int depth;

    /* 4:3: the native frame is the 4:3 picture, nothing is outside the one
       and inside the other */
    for (depth = 0; depth < 2; depth++) {
        for (dy = -400; dy <= 400; dy += 8) {
            for (dx = -3000; dx <= 3000; dx++) {
                CHECK(ico_demo_walk_should_start((float)dx, (float)dy, depth, 512, 448) == 0);
            }
        }
    }
    CHECK(ico_demo_walk_should_start(256.5f, 0.0f, 1, 512, 448) == 0);

    /* 48:9 (2048 wide): the band beside the 4:3 picture */
    CHECK(ico_demo_walk_should_start(300.0f, 0.0f, 1, 2048, 448) == 1);
    CHECK(ico_demo_walk_should_start(-300.0f, 100.0f, 1, 2048, 448) == 1);
    CHECK(ico_demo_walk_should_start(1024.0f, 0.0f, 1, 2048, 448) == 1);
    CHECK(ico_demo_walk_should_start(256.0f, 0.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_start(100.0f, 0.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_start(1025.0f, 0.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_start(300.0f, 225.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_start(300.0f, 0.0f, 0, 2048, 448) == 0);
    /* 16:9 (688 wide) */
    CHECK(ico_demo_walk_should_start(300.0f, 0.0f, 1, 688, 448) == 1);
    CHECK(ico_demo_walk_should_start(345.0f, 0.0f, 1, 688, 448) == 0);

    /* the hold: past the native side by the margin, or behind */
    CHECK(ico_demo_walk_should_stop(1100.0f, 0.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_stop(1024.0f + ICO_DEMO_WALK_MARGIN, 0.0f, 1, 2048, 448) == 0);
    CHECK(ico_demo_walk_should_stop(1024.0f + ICO_DEMO_WALK_MARGIN + 1.0f, 0.0f, 1, 2048, 448) ==
          1);
    CHECK(ico_demo_walk_should_stop(-1200.0f, 0.0f, 1, 2048, 448) == 1);
    CHECK(ico_demo_walk_should_stop(300.0f, 224.0f + ICO_DEMO_WALK_MARGIN + 1.0f, 1, 2048, 448) ==
          1);
    CHECK(ico_demo_walk_should_stop(300.0f, 0.0f, 0, 2048, 448) == 1);
}

/* the root's screen x a tick after the stop: starts at x0, moves dxPerFrame */
static float screenX(float x0, float dxPerFrame, float e)
{
    return x0 + dxPerFrame * e;
}

static void testWalk(void)
{
    IcoDemoWalkPlan p;
    IcoDemoWalkActor a;
    Synth sa = {250, 4, 40, 130, 170, {-2.0f, 0.0f, 2.0f}};
    float root[3];
    float pf;
    float e;
    float frame;
    float play;
    int w;

    CHECK(ico_demo_walk_analyse(&p, sa.frames, sa.nodes, &sa, synthRoot, synthPose) == 1);

    /* 4:3: the motion's own keys play at every frame, wherever the root is */
    for (w = 0; w < 3; w++) {
        float x0 = w == 0 ? 200.0f : (w == 1 ? 300.0f : 900.0f);

        memset(&a, 0, sizeof(a));
        for (frame = 0.0f, play = 0.0f; frame < 600.0f; frame += 0.5f, play += 0.5f) {
            float f = frame < 249.0f ? frame : 249.0f;
            float x = f < 130.0f ? x0 : screenX(x0, 6.0f, f - 130.0f);

            CHECK(ico_demo_walk_update(&a, &p, f, play, x, 0.0f, 1, 512, 448) < 0.0f);
        }
        CHECK(a.state == ICO_DEMO_WALK_IDLE || a.state == ICO_DEMO_WALK_DECLINED);
    }

    /* 48:9: before the stop the keys play */
    memset(&a, 0, sizeof(a));
    CHECK(ico_demo_walk_update(&a, &p, 0.0f, 0.0f, 400.0f, 0.0f, 1, 2048, 448) < 0.0f);
    CHECK(ico_demo_walk_update(&a, &p, 129.5f, 129.5f, 400.0f, 0.0f, 1, 2048, 448) < 0.0f);
    /* the first tick at the stop, the root in the band: the walk starts */
    e = ico_demo_walk_update(&a, &p, 130.5f, 130.5f, 400.0f, 0.0f, 1, 2048, 448);
    CHECK(a.state == ICO_DEMO_WALK_WALKING);
    CHECK(NEAR(e, 0.5f, 1e-4f));
    /* it walks on the play clock, the frame held or not */
    e = ico_demo_walk_update(&a, &p, 249.0f, 175.5f, 700.0f, 0.0f, 1, 2048, 448);
    CHECK(NEAR(e, 45.5f, 1e-4f));
    pf = ico_demo_walk_pose(&p, 45.0f, root);
    CHECK(NEAR(pf, 95.0f, 1e-4f)); /* the cycle 90..130, 5 frames in */
    CHECK(NEAR(root[0], p.root[0] - 90.0f, 1e-2f));
    CHECK(NEAR(root[1], p.root[1], 0.0f));
    CHECK(NEAR(root[2], p.root[2] + 90.0f, 1e-2f));
    pf = ico_demo_walk_pose(&p, 80.0f, root);
    CHECK(NEAR(pf, 90.0f, 1e-4f)); /* two cycles on: the cycle's first frame */
    /* past the native side by the margin: held there */
    e = ico_demo_walk_update(&a, &p, 249.0f, 260.0f, 1200.0f, 0.0f, 1, 2048, 448);
    CHECK(a.state == ICO_DEMO_WALK_HELD);
    CHECK(NEAR(e, 130.0f, 1e-4f));
    CHECK(NEAR(ico_demo_walk_update(&a, &p, 249.0f, 400.0f, 1500.0f, 0.0f, 1, 2048, 448), 130.0f,
               1e-4f));
    /* the same motion again from its start: the keys, then a new decision */
    CHECK(ico_demo_walk_update(&a, &p, 0.0f, 401.0f, 400.0f, 0.0f, 1, 2048, 448) < 0.0f);
    CHECK(a.state == ICO_DEMO_WALK_IDLE);

    /* inside the 4:3 picture at the stop: declined for good (the PS2 showed
       the stop), even when the camera later leaves him in the band */
    memset(&a, 0, sizeof(a));
    CHECK(ico_demo_walk_update(&a, &p, 130.0f, 130.0f, 100.0f, 0.0f, 1, 2048, 448) < 0.0f);
    CHECK(a.state == ICO_DEMO_WALK_DECLINED);
    CHECK(ico_demo_walk_update(&a, &p, 140.0f, 140.0f, 400.0f, 0.0f, 1, 2048, 448) < 0.0f);
    /* already past the native frame at the stop: declined */
    memset(&a, 0, sizeof(a));
    CHECK(ico_demo_walk_update(&a, &p, 130.0f, 130.0f, 1100.0f, 0.0f, 1, 2048, 448) < 0.0f);
    CHECK(a.state == ICO_DEMO_WALK_DECLINED);

    /* the cap: a root that never leaves holds after the most frames */
    memset(&a, 0, sizeof(a));
    CHECK(ico_demo_walk_update(&a, &p, 130.0f, 0.0f, 400.0f, 0.0f, 1, 2048, 448) >= 0.0f);
    e = ico_demo_walk_update(&a, &p, 249.0f, ICO_DEMO_WALK_MAX_FRAMES + 1.0f, 400.0f, 0.0f, 1, 2048,
                             448);
    CHECK(a.state == ICO_DEMO_WALK_HELD);
    CHECK(NEAR(e, ICO_DEMO_WALK_MAX_FRAMES + 1.0f, 1e-3f));
}

static void testHook(void)
{
    Synth sa = {250, 4, 40, 130, 170, {-2.0f, 0.0f, 2.0f}};
    Synth sb = {250, 4, 40, 130, 170, {-2.0f, 0.0f, 2.0f}};
    float pf = -1.0f;
    float root[3];
    int actorA;
    int actorB;
    int w;

    ico_demo_walk_reset();
    CHECK(ico_demo_walk_listed("D1_C23_SA") && ico_demo_walk_listed("D1_C23_SC") &&
          ico_demo_walk_listed("D1_C18_SB"));
    CHECK(!ico_demo_walk_listed("D1_C17_SA") && !ico_demo_walk_listed("D1_C23_B") &&
          !ico_demo_walk_listed(NULL));

    /* not listed: never, wherever the root is */
    CHECK(ico_demo_walk_tick(&actorA, 1001, "D1_C17_SA", &sa, 250, 4, synthRoot, synthPose, 140.0f,
                             140.0f, 400.0f, 0.0f, 1, 2048, 448, &pf, root) == 0);
    /* listed, at 4:3: never */
    for (w = 0; w < 300; w++) {
        CHECK(ico_demo_walk_tick(&actorA, 1004, "D1_C23_SA", &sa, 250, 4, synthRoot, synthPose,
                                 (float)w, (float)w, 300.0f + (float)w * 5.0f, 0.0f, 1, 512, 448,
                                 &pf, root) == 0);
    }
    /* listed, at 48:9: from the stop in the band */
    CHECK(ico_demo_walk_tick(&actorB, 1027, "D1_C23_SC", &sb, 250, 4, synthRoot, synthPose, 120.0f,
                             120.0f, 400.0f, 0.0f, 1, 2048, 448, &pf, root) == 0);
    CHECK(ico_demo_walk_tick(&actorB, 1027, "D1_C23_SC", &sb, 250, 4, synthRoot, synthPose, 130.0f,
                             130.0f, 400.0f, 0.0f, 1, 2048, 448, &pf, root) == 1);
    CHECK(NEAR(pf, 90.0f, 1e-4f));
    CHECK(ico_demo_walk_tick(&actorB, 1027, "D1_C23_SC", &sb, 250, 4, synthRoot, synthPose, 150.0f,
                             150.0f, 500.0f, 0.0f, 1, 2048, 448, &pf, root) == 1);
    CHECK(NEAR(pf, 110.0f, 1e-4f));
    CHECK(NEAR(root[0], 100.0f - 260.0f - 40.0f, 1e-2f));
    /* a new motion on the actor starts over */
    CHECK(ico_demo_walk_tick(&actorB, 1004, "D1_C23_SA", &sa, 250, 4, synthRoot, synthPose, 20.0f,
                             151.0f, 500.0f, 0.0f, 1, 2048, 448, &pf, root) == 0);
}

int main(void)
{
    testPlan();
    testDecision();
    testWalk();
    testHook();
    if (failures) {
        fprintf(stderr, "demo_walk_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("demo_walk_test: ok\n");
    return 0;
}
