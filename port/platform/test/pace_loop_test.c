/*
 * port/platform/test/pace_loop_test.c
 *
 * The vsync lock's loop (pace_policy.h pace_lock_vsync, issue 74) under a
 * fake clock and a fake FIFO present: a present costs the replay's time,
 * then blocks until the first vblank after it (a display running at its
 * own rate), or returns at once (a compositor that does not block).  Each
 * simulated vsync the test calls the loop as window_host.c's pace() does,
 * then runs the game's step (its own cost); a game frame closes every two
 * steps.
 *
 *   60 Hz on 59.94: the step is never late (it starts within 0.5 ms of the
 *     vblank the vsync's last present returned on), one present a refresh
 *     (3600 +- 1 in 60 s), the alphas exactly 1/2, 1 a frame and strictly
 *     increasing inside it, the audio's ratio 60 / 59.94;
 *   120 Hz: two presents a vsync, alphas 1/4, 2/4, 3/4, 4/4;
 *   return jitter of 0.4 ms keeps the lock; a 30 ms step is caught up
 *     (a step without a present), the steps stay one a vblank;
 *   a present that returns at once drops the lock after three misses;
 *   a display 2 % slower than it reports drops it, 1 % does not;
 *   the v0.6.1 loop (the step on the simulation's own 59.94 Hz timer,
 *     presenting while the last present returned before the deadline) ran
 *     the step a refresh late on a 60.00 Hz display: the model here shows it.
 *
 * A queued FIFO swapchain (Fake.depth > 0: up to depth pictures wait for the
 * display, one leaves at each vblank; a present returns at once while there
 * is room and waits for a vblank when there is none, as on NVIDIA, whose
 * acquire does not wait): the lock holds from an empty queue at the start,
 * after a 300 ms step, after a swapchain rebuild (quick, and one long enough
 * to be a stall), at 60 and 120 Hz, depth 2 and 3 (the v0.6.2 rc1 lock took
 * every at-once return from the empty queue for a miss, slept a period, let
 * the display drain the queue again, and dropped about 100 ms in).
 * A window minimised or occluded for 10 s (presents return at once and are
 * reported not shown) keeps the lock; judged, the same presents drop it.
 * The first frame after a reset has the alphas 1/2, 1 (not 1, 1).
 * Without the lock, under FIFO at 144 Hz with the game behind (a step
 * longer than the period), every new frame is still presented
 * (pace_fifo_hold), where the rc1 guard presented none.
 */
#include <stdio.h>
#include "pace_policy.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define MS 1000000ull
#define SEC 1000000000ull
#define NTSC 16683333ull
#define ALPHA_LOG 64

typedef struct Fake {
    uint64_t now;
    uint64_t phase, display; /* vblanks at phase + k * display */
    uint64_t cost;           /* a replay's CPU time */
    uint64_t jitterMax;      /* a present returns up to this after its vblank */
    uint32_t rng;
    int atOnce; /* the present does not block */
    /* a queued swapchain (0: the present blocks to the first vblank after
       the replay): pictures waiting for the display, one leaves a vblank */
    unsigned depth, queued;
    uint64_t vbIdx; /* the vblank the queue was last drained to */
    /* the window off the screen for vsyncs [hiddenFrom, hiddenTo): the
       present returns at once and is not shown */
    unsigned vsync, hiddenFrom, hiddenTo;
    int ignoreShown; /* the loop is not told (the rc1 window) */
    /* a swapchain rebuild at vsync rebuildAt: the queue empty, rebuildNs
       spent */
    unsigned rebuildAt;
    uint64_t rebuildNs;
    unsigned presents;
    uint64_t lastVblank; /* the vblank the last present returned on */
    /* the alphas since the last frame closed */
    float alpha[ALPHA_LOG];
    unsigned nAlpha;
} Fake;

static uint64_t f_now(void *u)
{
    return ((Fake *)u)->now;
}

static uint32_t f_rand(Fake *f)
{
    f->rng = f->rng * 1664525u + 1013904223u;
    return f->rng >> 8;
}

static int f_hidden(const Fake *f)
{
    return f->vsync >= f->hiddenFrom && f->vsync < f->hiddenTo;
}

/* the queue after the vblanks up to now */
static void f_drain(Fake *f)
{
    const uint64_t cur = (f->now - f->phase) / f->display;
    const uint64_t pop = cur - f->vbIdx;

    f->queued = pop >= f->queued ? 0 : f->queued - (unsigned)pop;
    f->vbIdx = cur;
}

static bool f_present(void *u, float a)
{
    Fake *f = u;

    f->now += f->cost;
    f->presents++;
    if (f->nAlpha < ALPHA_LOG) {
        f->alpha[f->nAlpha++] = a;
    }
    if (f->atOnce || f_hidden(f)) {
        f->lastVblank = f->now;
        return true;
    }
    if (f->depth > 0) {
        f_drain(f);
        if (f->queued >= f->depth) {
            /* full: wait for the vblank that takes one */
            f->now = f->phase + (f->vbIdx + 1) * f->display;
            f_drain(f);
            f->lastVblank = f->now;
            f->now += f->jitterMax ? f_rand(f) % f->jitterMax : 0;
        } else {
            f->lastVblank = f->now; /* at once: no vblank waited for */
        }
        f->queued++;
        return true;
    }
    /* the first vblank after the replay */
    const uint64_t k = (f->now - f->phase) / f->display + 1;

    f->lastVblank = f->phase + k * f->display;
    f->now = f->lastVblank + (f->jitterMax ? f_rand(f) % f->jitterMax : 0);
    return true;
}

static bool f_shown(void *u)
{
    return !f_hidden(u);
}

static void f_sleep(void *u, uint64_t ns)
{
    Fake *f = u;

    if (ns > f->now) {
        f->now = ns;
    }
}

typedef struct Run {
    unsigned steps, presents, debtSteps;
    uint64_t lateMax;      /* the step's start after the vblank its last present returned on */
    int alphaBad;          /* an alpha not the exact fraction, or not increasing in a frame */
    unsigned alphaChecked; /* the alphas checked */
    int dropped;           /* PaceLock.dropped at the end */
    unsigned dropVsync;    /* the vsync it dropped at (0: held) */
    double ratio;          /* pace_lock_rate_ratio over the last 10 s */
    unsigned fillPresents, unshown, gridSteps;
} Run;

/* seconds of simulated vsyncs: the loop, then the step (stepNs; bigStep at
   vsync bigAt) */
static Run run(Fake *f, unsigned n, uint64_t refreshNominal, uint64_t stepNs, double seconds,
               unsigned bigAt, uint64_t bigStep)
{
    PaceLock L;
    const PaceLockIo io = {f, f_now, f_present, f_sleep, f->ignoreShown ? NULL : f_shown};
    const uint64_t start = f->now, end = start + (uint64_t)(seconds * 1e9);
    Run r = {0};
    unsigned vsync = 0;
    bool newFrame = true;
    unsigned presentsAtStep = 0, fillAtFrame = 0;

    pace_lock_reset(&L, n, refreshNominal, NTSC, f->now);
    if (f->depth > 0) {
        f_drain(f); /* an empty queue at the start */
    }
    while (f->now < end) {
        const unsigned before = L.debtSteps, gridBefore = L.gridSteps;
        const uint64_t ratioAt = start + (uint64_t)((seconds - 10.0) * 1e9);

        f->vsync = vsync;
        if (f->rebuildAt != 0 && vsync == f->rebuildAt) {
            /* the swapchain made again: an empty queue */
            f->now += f->rebuildNs;
            f_drain(f);
            f->queued = 0;
        }
        if (newFrame) {
            /* the frame that just closed: its alphas were 1 / (2n) .. 1 */
            for (unsigned i = 0; i < f->nAlpha; i++) {
                const float want = pace_lock_alpha(i + 1, n, 2);

                if (vsync <= 4 || L.debtSteps != 0 || L.gridSteps != 0 ||
                    L.fillPresents != fillAtFrame) {
                    continue;
                }
                r.alphaChecked++;
                if (f->alpha[i] != want || (i > 0 && !(f->alpha[i] > f->alpha[i - 1]))) {
                    r.alphaBad++;
                }
            }
            f->nAlpha = 0;
            fillAtFrame = L.fillPresents;
        }
        if (f->now < ratioAt + NTSC && f->now >= ratioAt) {
            (void)pace_lock_rate_ratio(&L); /* the block starts */
        }
        (void)pace_lock_vsync(&L, &io, newFrame);
        vsync++;
        if (L.dropped) {
            r.dropped = L.dropped;
            r.dropVsync = vsync;
            break;
        }
        /* the step starts now */
        if (L.debtSteps == before && L.gridSteps == gridBefore && f->presents != presentsAtStep &&
            !f->atOnce && f->now - f->lastVblank > r.lateMax) {
            r.lateMax = f->now - f->lastVblank;
        }
        presentsAtStep = f->presents;
        r.steps++;
        f->now += vsync == bigAt ? bigStep : stepNs;
        newFrame = vsync % 2 == 0; /* a frame every two steps */
    }
    r.presents = f->presents;
    r.debtSteps = L.debtSteps;
    r.fillPresents = L.fillPresents;
    r.unshown = L.unshown;
    r.gridSteps = L.gridSteps;
    r.ratio = pace_lock_rate_ratio(&L);
    return r;
}

static Fake fake(uint64_t display)
{
    Fake f = {0};

    f.now = 5 * SEC;
    f.phase = 5 * SEC - 13 * MS; /* the display's own phase (before the start) */
    f.display = display;
    f.cost = 2 * MS;
    f.rng = 12345;
    return f;
}

/* 60.00 Hz display (16.667 ms) under the 59.94 simulation */
static void test60(void)
{
    Fake f = fake(16666667ull);
    const Run r = run(&f, 1, 16666667ull, 3 * MS, 60.0, 0, 0);

    printf("60 Hz: %u presents, %u steps, late max %.3f ms, ratio %.6f\n", r.presents, r.steps,
           (double)r.lateMax / 1e6, r.ratio);
    CHECK(r.dropped == 0);
    CHECK(r.lateMax < MS / 2);
    CHECK(r.presents >= 3599 && r.presents <= 3601);
    CHECK(r.steps >= 3599 && r.steps <= 3601); /* one step a vblank: the display is the clock */
    CHECK(r.alphaBad == 0 && r.alphaChecked >= 3590);
    /* the audio plays 60 / 59.94 faster */
    CHECK(r.ratio > 1.000951 && r.ratio < 1.001051);
}

/* 59.94 Hz display: the simulation at its own rate, ratio 1 */
static void test5994(void)
{
    Fake f = fake(16683350ull);
    const Run r = run(&f, 1, 16683350ull, 3 * MS, 30.0, 0, 0);

    CHECK(r.dropped == 0);
    CHECK(r.lateMax < MS / 2);
    CHECK(r.ratio > 0.99995 && r.ratio < 1.00005);
}

/* 120 Hz: two presents a vsync, alphas 1/4, 2/4, 3/4, 4/4 */
static void test120(void)
{
    Fake f = fake(8333333ull);
    Run r;

    f.cost = 1 * MS;
    r = run(&f, 2, 8333333ull, 3 * MS, 60.0, 0, 0);
    printf("120 Hz: %u presents, %u steps, late max %.3f ms\n", r.presents, r.steps,
           (double)r.lateMax / 1e6);
    CHECK(r.dropped == 0);
    CHECK(r.lateMax < MS / 2);
    CHECK(r.presents >= 7198 && r.presents <= 7202); /* a vsync's two at either end */
    CHECK(r.steps >= 3599 && r.steps <= 3601);
    CHECK(r.alphaBad == 0 && r.alphaChecked >= 7190);
    /* the alphas of the last frame, exactly */
    CHECK(pace_lock_alpha(1, 2, 2) == 0.25f && pace_lock_alpha(4, 2, 2) == 1.0f);
}

/* returns jitter by up to 0.4 ms: still held, never late past it */
static void testJitter(void)
{
    Fake f = fake(16666667ull);
    Run r;

    f.jitterMax = 400000ull;
    r = run(&f, 1, 16666667ull, 3 * MS, 60.0, 0, 0);
    CHECK(r.dropped == 0);
    CHECK(r.lateMax < MS / 2);
    CHECK(r.presents >= 3599 && r.presents <= 3601);
}

/* one 30 ms step: its vblanks are caught up by a step without a present,
   the lock holds and the steps stay one a vblank */
static void testStall(void)
{
    Fake f = fake(16666667ull);
    const Run r = run(&f, 1, 16666667ull, 3 * MS, 20.0, 600, 30 * MS);

    CHECK(r.dropped == 0);
    CHECK(r.debtSteps == 1);
    CHECK(r.steps >= 1199 && r.steps <= 1201);
    CHECK(r.presents >= 1198 && r.presents <= 1200);
}

/* a present that returns at once: three misses drop the lock */
static void testAtOnce(void)
{
    Fake f = fake(16666667ull);
    Run r;

    f.atOnce = 1;
    r = run(&f, 1, 16666667ull, 3 * MS, 10.0, 0, 0);
    printf("at once: dropped %d at vsync %u\n", r.dropped, r.dropVsync);
    CHECK(r.dropped == PACE_LOCK_DROP_AT_ONCE);
    CHECK(r.dropVsync >= 3 && r.dropVsync <= 8);
}

/* a display 2 % slower than it reports drops the lock; 1 % does not */
static void testSlowDisplay(void)
{
    /* 2 % of 16.666667 ms is 0.3333333 ms: 17.00001 and 16.333333 ms (a few
       ns past the edge: the median of an even count rounds down) */
    Fake f = fake(17000010ull);
    Run r = run(&f, 1, 16666667ull, 3 * MS, 10.0, 0, 0);

    printf("2 %% slow: dropped %d at vsync %u\n", r.dropped, r.dropVsync);
    CHECK(r.dropped == PACE_LOCK_DROP_RATE);
    f = fake(16666667ull * 101 / 100);
    r = run(&f, 1, 16666667ull, 3 * MS, 10.0, 0, 0);
    CHECK(r.dropped == 0);
    f = fake(16333333ull);
    r = run(&f, 1, 16666667ull, 3 * MS, 10.0, 0, 0);
    CHECK(r.dropped == PACE_LOCK_DROP_RATE);
}

/* A present that did nothing (a movie on the output): the vsync runs on the
   simulation's grid, a period after the last step, and the lock holds */
static bool f_none(void *u, float a)
{
    (void)u;
    (void)a;
    return false;
}

static void testNothingPresented(void)
{
    Fake f = fake(16666667ull);
    PaceLock L;
    const PaceLockIo io = {&f, f_now, f_none, f_sleep, NULL};
    uint64_t due;

    pace_lock_reset(&L, 1, 16666667ull, NTSC, f.now);
    due = pace_lock_vsync(&L, &io, true);
    CHECK(due == 5 * SEC + NTSC);
    CHECK(f.now == due);
    f.now += 3 * MS;
    due = pace_lock_vsync(&L, &io, false);
    CHECK(due == 5 * SEC + 2 * NTSC);
    CHECK(L.dropped == 0 && L.misses == 0);
}

/* The v0.6.1 loop, modelled: the deadline advances by the simulation's
   period; while the vsync's present returned before it, present again (a
   blocking FIFO present).  Returns the latest step after its deadline. */
static uint64_t oldLoopLateMax(uint64_t display, double seconds)
{
    Fake f = fake(display);
    uint64_t deadline = f.now, lateMax = 0;
    const uint64_t end = f.now + (uint64_t)(seconds * 1e9);
    int shown = 0;

    while (f.now < end) {
        deadline += NTSC;
        shown = 0;
        for (;;) {
            if (shown && f.now >= deadline) {
                break;
            }
            f_present(&f, 0.5f);
            f.nAlpha = 0;
            shown = 1;
        }
        if (f.now - deadline > lateMax) {
            lateMax = f.now - deadline;
        }
        f.now += 3 * MS;
    }
    return lateMax;
}

static void testOldLoop(void)
{
    const uint64_t late = oldLoopLateMax(16666667ull, 30.0);

    printf("v0.6.1 loop on 60.00 Hz: step late max %.1f ms\n", (double)late / 1e6);
    CHECK(late > 10 * MS); /* a whole refresh late: what issue 74 saw */
}

/* A queued FIFO swapchain (B1 of the v0.6.2 review): depth 2 and 3, a
   2 ms step (the reviewer's model), the lock from an empty queue */
static void testQueue(unsigned depth, unsigned n, uint64_t display, uint64_t cost)
{
    Fake f = fake(display);
    Run r;

    f.depth = depth;
    f.cost = cost;
    r = run(&f, n, display, 2 * MS, 30.0, 0, 0);
    printf("queue %u, %u a vsync: dropped %d at vsync %u, %u presents again while filling, "
           "late max %.3f ms\n",
           depth, n, r.dropped, r.dropVsync, r.fillPresents, (double)r.lateMax / 1e6);
    CHECK(r.dropped == 0);
    CHECK(r.fillPresents >= 1 && r.fillPresents <= 2 * PACE_LOCK_FILL_PRESENTS);
    CHECK(r.gridSteps <= 1);
    CHECK(r.lateMax < MS / 2);
    CHECK(r.steps >= 1799 - depth && r.steps <= 1801 + depth); /* a step a vblank, n vblanks */
    CHECK(r.alphaBad == 0);
}

/* a 300 ms step from the steady state (a stage load): the queue drains,
   the lock catches up and holds */
static void testQueueLongStep(unsigned depth)
{
    Fake f = fake(16666667ull);
    Run r;

    f.depth = depth;
    r = run(&f, 1, 16666667ull, 2 * MS, 20.0, 600, 300 * MS);
    printf("queue %u, a 300 ms step: dropped %d at vsync %u\n", depth, r.dropped, r.dropVsync);
    CHECK(r.dropped == 0);
    CHECK(r.debtSteps == PACE_LOCK_DEBT_VSYNCS);
}

/* a swapchain rebuild (a resize, a mode change): the queue starts empty */
static void testQueueRebuild(unsigned depth, uint64_t rebuildNs)
{
    Fake f = fake(16666667ull);
    Run r;

    f.depth = depth;
    f.rebuildAt = 900;
    f.rebuildNs = rebuildNs;
    r = run(&f, 1, 16666667ull, 2 * MS, 30.0, 0, 0);
    printf("queue %u, a rebuild of %.0f ms: dropped %d at vsync %u\n", depth,
           (double)rebuildNs / 1e6, r.dropped, r.dropVsync);
    CHECK(r.dropped == 0);
}

/* the window minimised or occluded for 10 s: its presents return at once
   (D3D12's DXGI_STATUS_OCCLUDED, Vulkan's 0x0 surface) and the window says
   they are not shown: not judged, the lock holds and is in force after */
static void testHidden(void)
{
    Fake f = fake(16666667ull);
    Run r;

    f.depth = 3;
    f.hiddenFrom = 300;
    f.hiddenTo = 900;
    r = run(&f, 1, 16666667ull, 2 * MS, 30.0, 0, 0);
    printf("hidden 10 s: dropped %d, %u vsyncs not shown, lateMax %.3f ms\n", r.dropped, r.unshown,
           (double)r.lateMax / 1e6);
    CHECK(r.dropped == 0);
    CHECK(r.unshown >= 595 && r.unshown <= 605);
    CHECK(r.lateMax < MS / 2);
    /* the rc1 window judged them: three misses */
    f = fake(16666667ull);
    f.depth = 3;
    f.hiddenFrom = 300;
    f.hiddenTo = 900;
    f.ignoreShown = 1;
    r = run(&f, 1, 16666667ull, 2 * MS, 30.0, 0, 0);
    CHECK(r.dropped == PACE_LOCK_DROP_AT_ONCE && r.dropVsync > 300 && r.dropVsync < 320);
}

/* The first frame after a reset (the lock starting, back from the
   background) is two vsyncs long like the rest: alphas 1/2, 1 (rc1 took
   the first close for a one-vsync frame: 1, 1, the picture held a vblank) */
static void testFirstFrame(void)
{
    Fake f = fake(16666667ull);
    PaceLock L;
    const PaceLockIo io = {&f, f_now, f_present, f_sleep, NULL};

    pace_lock_reset(&L, 1, 16666667ull, NTSC, f.now);
    (void)pace_lock_vsync(&L, &io, true);
    f.now += 2 * MS;
    (void)pace_lock_vsync(&L, &io, false);
    f.now += 2 * MS;
    CHECK(f.nAlpha == 2 && f.alpha[0] == 0.5f && f.alpha[1] == 1.0f);
    /* the next close measures it: still two */
    (void)pace_lock_vsync(&L, &io, true);
    CHECK(L.perFrame == 2);
}

/* Without the lock under FIFO at 144 Hz: the window's present loop for one
   simulated vsync, as window_host.c's pace() (the rule pace_fifo_hold; rc1
   is the same call with frameShown always true).  Returns the new frames
   presented over `vsyncs`, the step stepNs long. */
static unsigned fifoNewFramesShown(uint64_t stepNs, unsigned vsyncs, int rc1)
{
    Fake f = fake(6944444ull);
    uint64_t deadline = f.now, lastReturn = 0;
    unsigned frame = 0, shownFrame = ~0u, heldFrame = ~0u, shownCount = 0;

    for (unsigned v = 0; v < vsyncs; v++) {
        deadline += NTSC;
        if (v % 2 == 0) {
            frame++;
        }
        for (;;) {
            const bool shown = shownFrame == frame;

            if (pace_fifo_hold(f.now, lastReturn, f.display, deadline, rc1 ? true : shown,
                               heldFrame == frame)) {
                if (!shown) {
                    heldFrame = frame;
                }
                f_sleep(&f, deadline);
                break;
            }
            if (shown && f.now >= deadline) {
                break;
            }
            f_present(&f, 0.5f);
            f.nAlpha = 0;
            lastReturn = f.now;
            if (!shown) {
                shownCount++;
            }
            shownFrame = frame;
        }
        f.now += stepNs;
    }
    return shownCount;
}

static void testFifoHold(void)
{
    const uint64_t r = 6944444ull, d = 100 * MS;

    /* a frame already shown: held when the next vblank is past the deadline */
    CHECK(pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, true, false));
    CHECK(!pace_fifo_hold(d - 10 * MS, d - 11 * MS, r, d, true, false));
    /* a new frame before the deadline: held once */
    CHECK(pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, false, false));
    CHECK(!pace_fifo_hold(d - 3 * MS, d - 4 * MS, r, d, false, true));
    /* a new frame past the deadline (the game behind): never held */
    CHECK(!pace_fifo_hold(d + 1 * MS, d - 4 * MS, r, d, false, false));
    CHECK(!pace_fifo_hold(d + 1 * MS, 0, r, d, true, false));

    /* the game behind, a 20 ms step against the 16.68 ms period: every new
       frame shown; the rc1 rule showed none of them */
    const unsigned now = fifoNewFramesShown(20 * MS, 600, 0);
    const unsigned rc1 = fifoNewFramesShown(20 * MS, 600, 1);

    printf("fifo at 144, the game behind: %u of 300 new frames shown (rc1 %u)\n", now, rc1);
    CHECK(now == 300);
    CHECK(rc1 < 10);
    /* a 3 ms step: all shown, as before */
    CHECK(fifoNewFramesShown(3 * MS, 600, 0) == 300);
}

/* the audio's ratio from the lock's start: the nominal one */
static void testNominalRatio(void)
{
    PaceLock L;

    pace_lock_reset(&L, 1, 16666667ull, NTSC, 1);
    CHECK(pace_lock_nominal_ratio(&L) > 1.000999 && pace_lock_nominal_ratio(&L) < 1.001001);
    pace_lock_reset(&L, 2, 8333333ull, NTSC, 1);
    CHECK(pace_lock_nominal_ratio(&L) > 1.000999 && pace_lock_nominal_ratio(&L) < 1.001001);
    pace_lock_reset(&L, 1, 16683333ull, NTSC, 1);
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
    pace_lock_reset(&L, 1, 20000000ull, 20000000ull, 1); /* PAL on 50 */
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
    pace_lock_reset(&L, 1, 17500000ull, NTSC, 1); /* 4.7 % off: not trusted */
    CHECK(pace_lock_nominal_ratio(&L) == 1.0);
}

int main(void)
{
    test60();
    test5994();
    test120();
    testJitter();
    testStall();
    testAtOnce();
    testSlowDisplay();
    testNothingPresented();
    testOldLoop();
    testQueue(2, 1, 16666667ull, 2 * MS);
    testQueue(3, 1, 16666667ull, 2 * MS);
    testQueue(3, 2, 8333333ull, 1 * MS);
    testQueue(3, 2, 8333333ull, 2 * MS); /* the display drains one while it fills */
    testQueueLongStep(2);
    testQueueLongStep(3);
    testQueueRebuild(3, 2 * MS);
    testQueueRebuild(3, 40 * MS);
    testHidden();
    testFirstFrame();
    testFifoHold();
    testNominalRatio();
    if (fails) {
        printf("pace_loop_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_loop_test: ok\n");
    return 0;
}
