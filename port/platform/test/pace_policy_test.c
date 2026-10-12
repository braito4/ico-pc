/*
 * port/platform/test/pace_policy_test.c
 *
 * The slow-present decision (pace_policy.h): the threshold, entering after 3
 * presents above it and leaving below 80 % of it, no flicker on a borderline
 * sequence, one spike changing nothing, the injector's higher threshold.
 * Resolution "auto"'s steps (testAutoResolution).  The gap between presents
 * (testPresentGap).
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
#define REFRESH (16667ull * 1000)
#define PERIOD (16683ull * 1000)

static bool feed(PaceHist *h, uint64_t ms10, bool inj)
{
    return pace_slow_present(h, ms10 * MS / 10, REFRESH, PERIOD, inj);
}

/* The window's loop over pace_auto_resolution_step: a sample
   per present at 60 a second for sec seconds, each cost ms10 tenths of a
   ms; every 2 s a step decision, a new window, the step's time kept */
static int autoRun(PaceSamples *s, uint64_t *clock, int scale, float winScale, uint64_t ms10,
                   int sec, int *steps)
{
    if (s->firstNs == 0) {
        pace_samples_reset(s, *clock);
    }
    for (int i = 0; i < sec * 60; i++) {
        *clock += 16666667ull;
        pace_samples_add(s, *clock, ms10 * MS / 10);
        if (s->lastNs - s->firstNs >= PACE_AUTO_WINDOW_NS) {
            const int next = pace_auto_resolution_step(s, scale, winScale);
            if (next != scale) {
                scale = next;
                s->lastStepNs = *clock;
                ++*steps;
            }
            pace_samples_reset(s, *clock);
        }
    }
    return scale;
}

static void testAutoResolution(void)
{
    PaceSamples s = {0};
    uint64_t clock = 1000 * MS;
    int steps = 0, scale;

    s.budgetNs = 16666667ull; /* 1/60 s: 70 % is 11.67 ms */
    /* under the budget's 70 %: never a step, however long */
    scale = autoRun(&s, &clock, 0, 2.41f, 110, 20, &steps);
    CHECK(scale == 0 && steps == 0);
    /* over it: nothing before 2 s of samples, then one step; a 2400 x 1080
       window (2.41x) goes to 2x, not 3x (larger than the window) */
    pace_samples_reset(&s, clock);
    for (int i = 0; i < 100; i++) {
        clock += 16666667ull;
        pace_samples_add(&s, clock, 14 * MS);
    }
    CHECK(pace_auto_resolution_step(&s, 0, 2.41f) == 0); /* 1.65 s */
    pace_samples_reset(&s, clock);
    steps = 0;
    scale = autoRun(&s, &clock, 0, 2.41f, 140, 2, &steps);
    CHECK(scale == 2 && steps == 1);
    /* a 4K window (4.82x): window -> 3x */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        int n = 0;
        k.budgetNs = 16666667ull;
        CHECK(autoRun(&k, &c, 0, 4.82f, 140, 2, &n) == 3 && n == 1);
        /* then one step per 2 s: 6 s of slow presents reach 1x, no faster */
        CHECK(autoRun(&k, &c, 3, 4.82f, 140, 2, &n) == 2 && n == 2);
        CHECK(autoRun(&k, &c, 2, 4.82f, 140, 2, &n) == 1 && n == 3);
        /* the floor: 1x stays 1x */
        CHECK(autoRun(&k, &c, 1, 4.82f, 300, 10, &n) == 1 && n == 3);
    }
    /* at most one step per 2 s: a step at time t, then a full window of
       slow samples (over 2 s of them) ending under 2 s after it changes
       nothing */
    {
        PaceSamples k = {0};
        k.budgetNs = 16666667ull;
        k.lastStepNs = 10000 * MS;
        for (int i = 0; i < 66; i++) {
            pace_samples_add(&k, 8800 * MS + (uint64_t)i * 33 * MS, 20 * MS); /* to 10.945 s */
        }
        CHECK(pace_auto_resolution_step(&k, 3, 4.82f) == 3); /* 0.95 s after the step */
        pace_samples_add(&k, 12000 * MS, 20 * MS);
        CHECK(pace_auto_resolution_step(&k, 3, 4.82f) == 2); /* 2 s after it */
    }
    /* never back up: fast presents at 1x or 2x leave the scale */
    steps = 0;
    CHECK(autoRun(&s, &clock, 2, 2.41f, 10, 20, &steps) == 2 && steps == 0);
    CHECK(autoRun(&s, &clock, 1, 2.41f, 10, 20, &steps) == 1 && steps == 0);
    /* a window at 1x or below has no step below it */
    CHECK(autoRun(&s, &clock, 0, 1.0f, 300, 10, &steps) == 0 && steps == 0);
    /* a fixed 30 a second limit: its period is the budget (70 %: 23.3 ms) */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        int n = 0;
        k.budgetNs = 33333333ull;
        CHECK(autoRun(&k, &c, 0, 2.41f, 200, 6, &n) == 0 && n == 0);
        CHECK(autoRun(&k, &c, 0, 2.41f, 250, 2, &n) == 2 && n == 1);
    }
    /* the median: one slow present in eight changes nothing */
    {
        PaceSamples k = {0};
        uint64_t c = 1000 * MS;
        k.budgetNs = 16666667ull;
        for (int i = 0; i < 160; i++) {
            c += 16666667ull;
            pace_samples_add(&k, c, i % 8 == 0 ? 40 * MS : 5 * MS);
        }
        CHECK(pace_samples_median(&k) == 5 * MS);
        CHECK(pace_auto_resolution_step(&k, 0, 2.41f) == 0);
    }
}

/* pace_present_gap: "uncapped" with vsync presents a sixteenth of a refresh
   early, in mailbox mode as under FIFO, on every platform, and never fewer
   than sixty a second whatever refresh the display reports; a limit is its
   own period; "uncapped" without vsync has no gap */
static void testPresentGap(void)
{
    static const struct {
        int cap;
        bool uncappedVsync, mailbox;
        uint64_t refresh, gap;
    } t[] = {
        /* 60 Hz, 90 Hz (a Steam Deck OLED), 144 Hz */
        {-1, true, true, 16666667ull, 16666667ull - 16666667ull / 16},
        {-1, true, true, 11111111ull, 11111111ull - 11111111ull / 16},
        {-1, true, true, 6944444ull, 6944444ull - 6944444ull / 16},
        /* a screen reported at 30 Hz presents as if it ran at 60 in mailbox
           mode; under FIFO a present would block, so it keeps its rate */
        {-1, true, true, 33333333ull, 16666667ull - 16666667ull / 16},
        {-1, true, false, 33333333ull, 33333333ull - 33333333ull / 16},
        {-1, true, false, 20000000ull, 20000000ull - 20000000ull / 16},
        {-1, false, true, 11111111ull, 0},
        {0, false, false, 16666667ull, 0},
        {120, false, false, 11111111ull, 8333333ull},
        {60, false, false, 6944444ull, 16666666ull},
    };

    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        const uint64_t g =
            pace_present_gap(t[i].cap, t[i].uncappedVsync, t[i].mailbox, t[i].refresh);
        if (g != t[i].gap) {
            printf("FAIL present gap row %u: %llu, want %llu\n", i, (unsigned long long)g,
                   (unsigned long long)t[i].gap);
            fails++;
        }
    }
    /* a little under a refresh: never more than 16 presents in 15 refreshes,
       and a late present still leaves the next refresh a new picture */
    for (uint64_t r = 4000000ull; r <= 33333334ull; r += 1234567ull) {
        const uint64_t g = pace_present_gap(-1, true, true, r);
        const uint64_t e = r < 16666667ull ? r : 16666667ull;
        CHECK(g >= e * 15 / 16);
        CHECK(g < e);
        const uint64_t f = pace_present_gap(-1, true, false, r);
        CHECK(f >= r * 15 / 16);
        CHECK(f < r);
    }
}

/* The mode a numeric cap takes with vsync on (issue 74): a cap equal to the
   refresh within 0.5 Hz takes FIFO with no timer gap; above or below it,
   mailbox (above) with the cap's own gap */
static void testCapEqualsRefresh(void)
{
    const uint64_t r6000 = 16666667ull, r5994 = 16683350ull, r120 = 8333333ull, r11988 = 8341675ull,
                   r144 = 6944444ull;

    /* equal: 60 on 60.00 and 59.94, 120 on 120.00 and 119.88, 144 on 144 */
    CHECK(pace_cap_equals_refresh(60, r6000));
    CHECK(pace_cap_equals_refresh(60, r5994));
    CHECK(pace_cap_equals_refresh(120, r120));
    CHECK(pace_cap_equals_refresh(120, r11988));
    CHECK(pace_cap_equals_refresh(144, r144));
    /* the window's rule: equal -> gap 0 (FIFO); otherwise the cap's gap */
    CHECK(pace_present_gap(-1, true, true, r6000) != 0); /* uncapped: unchanged */
    /* above the refresh: not equal, the cap's own gap (mailbox) */
    CHECK(!pace_cap_equals_refresh(144, r6000));
    CHECK(!pace_cap_equals_refresh(144, r120));
    CHECK(!pace_cap_equals_refresh(240, r144));
    CHECK(pace_present_gap(144, false, true, r6000) == 1000000000ull / 144);
    CHECK(pace_present_gap(240, false, true, r144) == 1000000000ull / 240);
    /* below the refresh: 60 on 120, 59 on 60, 30 on 60 */
    CHECK(!pace_cap_equals_refresh(60, r120));
    CHECK(!pace_cap_equals_refresh(59, r6000));
    CHECK(!pace_cap_equals_refresh(30, r6000));
    CHECK(pace_present_gap(60, false, true, r120) == 16666666ull);
    /* the edges: 0.5 Hz either side of 60 still equal, a whole Hz not */
    CHECK(pace_cap_equals_refresh(60, 1000000000ull * 2 / 119));
    CHECK(pace_cap_equals_refresh(60, 1000000000ull * 2 / 121 + 1));
    CHECK(!pace_cap_equals_refresh(60, 1000000000ull / 61));
    CHECK(!pace_cap_equals_refresh(60, 1000000000ull / 59));
    /* no cap */
    CHECK(!pace_cap_equals_refresh(0, r6000));
    CHECK(!pace_cap_equals_refresh(-1, r6000));
    CHECK(!pace_cap_equals_refresh(60, 0));
}

/* The limiter's grid under a fake clock.  pace() in window_host.c loops per
   simulated vsync: a game step (stepNs, no presents), then presents due on
   the grid until the vsync's deadline; a present of a frame already shown
   that would end past the deadline is skipped.  newGrid 0 is the old
   scheduling (due at the last present's start + gap), 1 the grid. */
static double simulate(int cap, int newGrid, uint64_t stepNs, uint64_t costNs, double seconds)
{
    const uint64_t period = 16683333ull, gap = 1000000000ull / (uint64_t)cap;
    const uint64_t end = (uint64_t)(seconds * 1e9);
    uint64_t t = 1000000000ull, deadline = t, last = 0, next = 0;
    unsigned presents = 0;

    while (t < 1000000000ull + end) {
        int shown = 0;

        deadline += period;
        t += stepNs;
        for (;;) {
            const uint64_t due = newGrid ? next : (last ? last + gap : 0);

            if (shown && t + costNs > deadline) {
                break;
            }
            if (due && t < due) {
                if (due >= deadline) {
                    break;
                }
                t = due;
                continue;
            }
            last = t;
            next = pace_next_present(next, t, gap);
            t += costNs;
            presents++;
            shown = 1;
        }
        if (t < deadline) {
            t = deadline;
        }
    }
    return presents / seconds;
}

static void testLimiterGrid(void)
{
    uint64_t n = 0;

    CHECK(pace_next_present(0, 5000, 100) == 5100);
    CHECK(pace_next_present(1000, 1050, 100) == 1100); /* on the grid */
    CHECK(pace_next_present(1000, 1100, 100) == 1100); /* a gap late: still the grid */
    CHECK(pace_next_present(1000, 1101, 100) == 1201); /* more than a gap: restart */
    for (int i = 0; i < 1000; i++) {
        n = pace_next_present(n, n ? n + 30 : 1, 100); /* late by 30 every time: no drift */
    }
    CHECK(n == 1 + 100 * 1000);

    /* 4K-like costs: a 3 ms step and a 2.5 ms present up to 165 presents a
       second, a 1 ms present at 240 */
    static const int caps[] = {60, 120, 144, 165, 240};

    for (unsigned i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
        const double g =
            simulate(caps[i], 1, 3000000ull, caps[i] > 165 ? 1000000ull : 2500000ull, 10.0);

        if (g < caps[i] - 0.5 || g > caps[i] + 0.5) {
            printf("FAIL limiter cap %d: %.2f presents/s\n", caps[i], g);
            fails++;
        }
    }
    {
        const double g = simulate(144, 1, 3000000ull, 2500000ull, 10.0);
        const double o = simulate(144, 0, 3000000ull, 2500000ull, 10.0);

        CHECK(g >= 143.5 && g <= 144.5);
        CHECK(o < 143.0); /* the old scheduling fell short */
    }
}

int main(void)
{
    PaceHist h = {0};
    int i, flips;
    bool last;

    /* max(refresh, period) + period / 2, and that plus a refresh with an effects program */
    CHECK(pace_slow_threshold(REFRESH, PERIOD, false) == PERIOD + PERIOD / 2);
    CHECK(pace_slow_threshold(33333ull * 1000, PERIOD, false) == 33333ull * 1000 + PERIOD / 2);
    CHECK(pace_slow_threshold(REFRESH, PERIOD, true) == PERIOD + REFRESH + PERIOD / 2);
    /* 144 Hz and 50 Hz displays against the 60 Hz game period: never below
       the plain threshold (2 * refresh + period / 2 was, at 144 Hz) */
    CHECK(pace_slow_threshold(6944444ull, PERIOD, true) == PERIOD + 6944444ull + PERIOD / 2);
    CHECK(pace_slow_threshold(6944444ull, PERIOD, true) >
          pace_slow_threshold(6944444ull, PERIOD, false));
    CHECK(pace_slow_threshold(20000000ull, PERIOD, true) == 20000000ull + 20000000ull + PERIOD / 2);
    CHECK(pace_slow_threshold(20000000ull, PERIOD, true) >
          pace_slow_threshold(20000000ull, PERIOD, false));

    /* a steady vsync-blocked present (one refresh) is never slow */
    for (i = 0; i < 40; i++) {
        CHECK(!feed(&h, 167, false));
    }

    /* one spike among normal presents changes nothing */
    CHECK(!feed(&h, 1000, false));
    for (i = 0; i < 10; i++) {
        CHECK(!feed(&h, 167, false));
    }

    /* a sustained slow renderer (30 ms against a 25 ms threshold) turns it
       on within a few presents and keeps it on */
    h = (PaceHist){0};
    for (i = 0; i < 8; i++) {
        feed(&h, 167, false);
    }
    flips = 0;
    for (i = 0; i < 20; i++) {
        flips += feed(&h, 300, false) ? 1 : 0;
    }
    CHECK(h.slow);
    CHECK(flips >= 10);

    /* back to normal: off once the median is below 80 % of the threshold
       (20 ms), and not before */
    for (i = 0; i < 20; i++) {
        feed(&h, 167, false);
    }
    CHECK(!h.slow);

    /* hysteresis: in the slow state, a cost between 80 % and 100 % of the
       threshold (22 ms against 25 ms) stays slow ... */
    h = (PaceHist){0};
    for (i = 0; i < 12; i++) {
        feed(&h, 300, false);
    }
    CHECK(h.slow);
    for (i = 0; i < 20; i++) {
        CHECK(feed(&h, 220, false));
    }
    /* ... and in the normal state the same cost stays normal */
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        CHECK(!feed(&h, 220, false));
    }

    /* a borderline sequence around the threshold (24 and 26 ms alternating)
       does not flicker: at most one change in 40 presents */
    h = (PaceHist){0};
    flips = 0;
    last = false;
    for (i = 0; i < 40; i++) {
        const bool s = feed(&h, i % 2 ? 260 : 240, false);
        flips += s != last;
        last = s;
    }
    CHECK(flips <= 1);

    /* the same with random-looking jitter straddling the threshold */
    h = (PaceHist){0};
    flips = 0;
    last = false;
    for (i = 0; i < 80; i++) {
        static const unsigned c[] = {230, 270, 250, 260, 240, 280, 220, 255};
        const bool s = feed(&h, c[i % 8], false);
        flips += s != last;
        last = s;
    }
    CHECK(flips <= 1);

    /* an effects program: 30 ms is slow without one, not slow with one
       (threshold 2 * 16.7 + 8.3 = 41.7 ms); 50 ms is slow with one */
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        CHECK(!feed(&h, 300, true));
    }
    h = (PaceHist){0};
    for (i = 0; i < 20; i++) {
        feed(&h, 500, true);
    }
    CHECK(h.slow);

    testAutoResolution();
    testPresentGap();
    testCapEqualsRefresh();
    testLimiterGrid();

    if (fails) {
        printf("pace_policy_test: %d failures\n", fails);
        return 1;
    }
    printf("pace_policy_test: ok\n");
    return 0;
}
