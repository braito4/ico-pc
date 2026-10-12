/*
 * port/platform/pace_policy.c
 *
 * The slow-present decision (pace_policy.h).
 */
#include "pace_policy.h"

#include <stddef.h>
#include <stdio.h>

uint64_t pace_slow_threshold(uint64_t refreshNs, uint64_t periodNs, bool injector)
{
    if (injector) {
        /* one refresh more than the plain threshold */
        return (refreshNs > periodNs ? refreshNs : periodNs) + refreshNs + periodNs / 2;
    }
    return (refreshNs > periodNs ? refreshNs : periodNs) + periodNs / 2;
}

uint64_t pace_present_gap(int cap, bool uncappedVsync, bool mailbox, uint64_t refreshNs)
{
    if (uncappedVsync) {
        /* one present a refresh, but never fewer than sixty a second: a
           display can report a refresh it is not running at (a 4K screen
           the user plays at 60 Hz reported 30.0 Hz, and one present per
           reported refresh halved what the screen showed), and sixty a
           second on a true 30 Hz screen only costs what v0.5.1 spent on
           every screen. A sixteenth of a refresh early: the sleep's
           overshoot and a game step that a due present lands in only make
           a present later, and the gap counts from the last present's
           start, so a gap of exactly one refresh averaged more than one */
        /* Only in mailbox mode, where a present never blocks: under FIFO a
           present past the screen's own rate waits for the next refresh and
           would hold the game's step with it. */
        const uint64_t sixty = 16666667ull;
        const uint64_t r = mailbox && refreshNs > sixty ? sixty : refreshNs;
        return r - r / 16;
    }
    return cap > 0 ? 1000000000ull / (uint64_t)cap : 0;
}

bool pace_gap_as_uncapped(bool uncapped, bool lockDropped, bool wanted, bool mailboxNow)
{
    return wanted && (uncapped || (lockDropped && mailboxNow));
}

bool pace_cap_equals_refresh(int cap, uint64_t refreshNs)
{
    if (cap <= 0 || refreshNs == 0) {
        return false;
    }
    /* the reported rate in mHz, rounded */
    const uint64_t mhz = (1000000000000ull + refreshNs / 2) / refreshNs;
    const uint64_t frac = mhz % 1000;
    const uint64_t c = (uint64_t)cap * 1000ull;
    /* a whole number (to 0.01 Hz) of 100 Hz or more may be a rate cut down:
       up to 1 Hz above it (below, SDL reports 59 as 59.94: a whole 59 is a
       true 59.00 Hz mode) */
    const uint64_t above = mhz >= 99990 && (frac <= 10 || frac >= 990) ? 1000 : 500;

    return c + 500 >= mhz && c <= mhz + above;
}

bool pace_fifo_hold(uint64_t nowNs, uint64_t lastReturnNs, uint64_t refreshNs, uint64_t deadlineNs,
                    bool frameShown, bool frameHeld)
{
    if (lastReturnNs == 0 || refreshNs == 0 || nowNs < lastReturnNs) {
        return false;
    }
    /* a new frame: shown once the deadline has passed, held at most once */
    if (!frameShown && (nowNs >= deadlineNs || frameHeld)) {
        return false;
    }
    /* the vblank a present made now returns on */
    const uint64_t k = (nowNs - lastReturnNs) / refreshNs + 1;

    return lastReturnNs + k * refreshNs > deadlineNs;
}

uint64_t pace_next_present(uint64_t nextPresent, uint64_t startNs, uint64_t gap)
{
    const uint64_t n = nextPresent + gap;

    if (nextPresent == 0 || startNs > n) {
        return startNs + gap;
    }
    return n;
}

/* the median of n costs (n <= PACE_AUTO_SAMPLES; 0 with none) */
static uint64_t median_of(const uint64_t *cost, unsigned n)
{
    uint64_t v[PACE_AUTO_SAMPLES];
    unsigned i, j;
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        v[i] = cost[i];
    }
    for (i = 1; i < n; i++) {
        const uint64_t x = v[i];
        for (j = i; j > 0 && v[j - 1] > x; j--) {
            v[j] = v[j - 1];
        }
        v[j] = x;
    }
    /* an even count: the mean of the middle two */
    return n % 2 ? v[n / 2] : v[n / 2 - 1] / 2 + v[n / 2] / 2;
}

static uint64_t median(const PaceHist *h)
{
    return median_of(h->cost, h->count);
}

bool pace_slow_present(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                       bool injector)
{
    const uint64_t thr = pace_slow_threshold(refreshNs, periodNs, injector);
    uint64_t med;

    h->cost[h->next] = costNs;
    h->next = (h->next + 1) % PACE_HIST_LEN;
    if (h->count < PACE_HIST_LEN) {
        h->count++;
    }
    med = median(h);
    if (h->slow) {
        if (med < thr / 10 * 8) {
            h->slow = false;
            h->over = 0;
        }
    } else if (med > thr) {
        if (++h->over >= PACE_ENTER_COUNT) {
            h->slow = true;
        }
    } else {
        h->over = 0;
    }
    return h->slow;
}

/* resolution "auto": the cost samples */
void pace_samples_reset(PaceSamples *s, uint64_t nowNs)
{
    s->count = s->next = 0;
    s->firstNs = s->lastNs = nowNs;
}

void pace_samples_add(PaceSamples *s, uint64_t nowNs, uint64_t costNs)
{
    if (s->firstNs == 0) {
        s->firstNs = nowNs;
    }
    s->lastNs = nowNs;
    s->cost[s->next] = costNs;
    s->next = (s->next + 1) % PACE_AUTO_SAMPLES;
    if (s->count < PACE_AUTO_SAMPLES) {
        s->count++;
    }
}

uint64_t pace_samples_median(const PaceSamples *s)
{
    return median_of(s->cost, s->count);
}

int pace_auto_resolution_step(const PaceSamples *s, int currentScale, float windowScale)
{
    if (s->count < PACE_AUTO_MIN_SAMPLES || s->lastNs - s->firstNs < PACE_AUTO_WINDOW_NS ||
        s->budgetNs == 0) {
        return currentScale;
    }
    if (s->lastStepNs != 0 && s->lastNs - s->lastStepNs < PACE_AUTO_WINDOW_NS) {
        return currentScale;
    }
    /* over 70 % of the budget */
    if (pace_samples_median(s) * 10 <= s->budgetNs * 7) {
        return currentScale;
    }
    if (currentScale <= 0) {
        /* the window's size: the largest of 3x, 2x, 1x below it (a window
           at 1x or less has nothing below it: the scene is never smaller
           than the game's own) */
        for (int n = 3; n >= 1; n--) {
            if ((float)n < windowScale - 0.05f) {
                return n;
            }
        }
        return currentScale;
    }
    if (currentScale > 3) {
        return 3;
    }
    return currentScale > 1 ? currentScale - 1 : 1;
}

/* ------------------------------------------------------------ the vsync lock */

unsigned pace_lock_presents(int cap, uint64_t periodNs)
{
    if (cap <= 0 || periodNs == 0) {
        return 0;
    }
    /* cap / simulated rate = cap * period / 1e9 */
    const uint64_t x = (uint64_t)cap * periodNs;
    const uint64_t n = (x + 500000000ull) / 1000000000ull;

    if (n == 0) {
        return 0;
    }
    /* within 1 % of n rates: |x - n e9| <= x / 100 */
    const uint64_t d = x > n * 1000000000ull ? x - n * 1000000000ull : n * 1000000000ull - x;
    return d * 100 <= x ? (unsigned)n : 0;
}

bool pace_lock_rate_ok(uint64_t intervalNs, uint64_t refreshNs)
{
    const uint64_t d = intervalNs > refreshNs ? intervalNs - refreshNs : refreshNs - intervalNs;

    return d * 50 < refreshNs;
}

float pace_lock_alpha(unsigned k, unsigned n, unsigned perFrame)
{
    if (n == 0 || perFrame == 0) {
        return 0.0f;
    }
    const unsigned d = n * perFrame;

    return k >= d ? 1.0f : (float)k / (float)d;
}

void pace_lock_reset(PaceLock *L, unsigned n, uint64_t refreshNs, uint64_t periodNs, uint64_t nowNs)
{
    *L = (PaceLock){0};
    L->n = n;
    L->refreshNs = refreshNs;
    L->periodNs = periodNs;
    L->due = nowNs;
    L->perFrame = 2; /* a game frame every two vsyncs until two closes are seen */
    L->rateClean = true;
    L->filling = true; /* the queue may be empty (the start, a rebuild, the background) */
}

PaceLockVerdict pace_lock_judge(PaceLock *L, uint64_t returnNs, unsigned *vblanks)
{
    const uint64_t r = L->refreshNs;
    const uint64_t last = L->lastReturn;
    const bool unjudged = L->unjudged || last == 0 || returnNs < last;
    const bool fromVblank = L->onVblank;

    L->lastReturn = returnNs;
    L->unjudged = false;
    L->onVblank = false;
    *vblanks = 1;
    if (unjudged) {
        return PACE_LOCK_UNJUDGED;
    }
    const uint64_t iv = returnNs - last;

    if (iv * 2 < r) {
        *vblanks = 0;
        return PACE_LOCK_MISS_AT_ONCE;
    }
    if (iv * 2 >= 3 * r) {
        /* a stall: the queue drained meanwhile (this return may not have
           waited for a vblank) */
        *vblanks = (unsigned)((iv + r / 2) / r);
        L->filling = true;
        return PACE_LOCK_STALL;
    }
    L->onVblank = true;
    if (!fromVblank) {
        /* the interval started at a return that may not have been on a
           vblank (the first, after a sleep, at once): one vblank, not a
           measure of the refresh */
        return PACE_LOCK_UNJUDGED;
    }
    if (L->settled < PACE_LOCK_SETTLE) {
        /* the first second after a (re)start: the queue and the work
           settle; nothing summed */
        L->settled++;
        return PACE_LOCK_HIT;
    }
    /* the rate: the mean over a window of intervals, whose sum is the
       window's last return less its first, so a change of the work (which
       moves every return after a vblank) changes it by that change over the
       window, not by that change an interval */
    L->rateSumNs += iv;
    if (++L->rateCount < PACE_LOCK_RATE_WINDOW) {
        return PACE_LOCK_HIT;
    }
    L->rateMeanNs = L->rateSumNs / L->rateCount;
    L->rateSumNs = 0;
    L->rateCount = 0;
    if (pace_lock_rate_ok(L->rateMeanNs, r)) {
        L->rateOut = 0;
        return PACE_LOCK_HIT;
    }
    L->rateOut++;
    return PACE_LOCK_MISS_RATE;
}

/* the trace: a present's interval and letter */
static void lock_trace(PaceLock *L, uint64_t ivNs, char v)
{
    const uint64_t us = ivNs / 1000u;
    const uint32_t u = us > 0xffffffffu ? 0xffffffffu : (uint32_t)us;

    if (L->traceFirstCount < PACE_LOCK_TRACE_FIRST) {
        L->traceFirstUs[L->traceFirstCount] = u;
        L->traceFirstV[L->traceFirstCount++] = v;
    }
    L->traceLastUs[L->traceLastNext] = u;
    L->traceLastV[L->traceLastNext] = v;
    L->traceLastNext = (L->traceLastNext + 1) % PACE_LOCK_TRACE_LAST;
    if (L->traceLastCount < PACE_LOCK_TRACE_LAST) {
        L->traceLastCount++;
    }
}

unsigned pace_lock_trace_text(const PaceLock *L, bool last, char *buf, unsigned len)
{
    const unsigned count = last ? L->traceLastCount : L->traceFirstCount;
    const unsigned first =
        last ? (L->traceLastNext + PACE_LOCK_TRACE_LAST - L->traceLastCount) % PACE_LOCK_TRACE_LAST
             : 0;
    unsigned at = 0, i;

    if (len == 0) {
        return 0;
    }
    buf[0] = '\0';
    for (i = 0; i < count; i++) {
        const unsigned k = last ? (first + i) % PACE_LOCK_TRACE_LAST : i;
        const uint32_t us = last ? L->traceLastUs[k] : L->traceFirstUs[k];
        const char v = last ? L->traceLastV[k] : L->traceFirstV[k];
        const int w = snprintf(buf + at, len - at, "%s%u.%02u%c", i ? " " : "", us / 1000u,
                               (us % 1000u + 5u) / 10u > 99u ? 99u : (us % 1000u + 5u) / 10u, v);

        if (w < 0 || (unsigned)w >= len - at) {
            buf[at] = '\0';
            break;
        }
        at += (unsigned)w;
    }
    return i;
}

/* a step at dueNs: counted, and the rate block's */
static uint64_t lock_step(PaceLock *L, uint64_t dueNs)
{
    L->due = dueNs;
    L->steps++;
    if (L->rateSteps == 0 && L->rateFirst == 0) {
        L->rateFirst = dueNs;
    } else {
        L->rateSteps++;
    }
    L->rateLast = dueNs;
    return dueNs;
}

/* this vsync on the simulation's own grid: the step due a period after the
   last; the next interval spans the sleep and is not judged */
static uint64_t lock_grid(PaceLock *L, const PaceLockIo *io, uint64_t now)
{
    uint64_t due = L->due + L->periodNs;

    if (now < due) {
        io->sleepUntil(io->user, due);
    } else if (now - due > L->periodNs) {
        /* more than a period behind: the lag is dropped */
        L->resyncs++;
        L->droppedNs += now - due;
        due = now;
    }
    if (L->vblanks < L->n) {
        L->sinceClose += L->n - L->vblanks; /* the vsync's share of the frame */
    }
    L->vblanks = 0;
    L->unjudged = true;
    L->filling = true; /* the display drained the queue while it slept */
    L->gridSteps++;
    L->rateClean = false;
    return lock_step(L, due);
}

uint64_t pace_lock_vsync(PaceLock *L, const PaceLockIo *io, bool newFrame)
{
    uint64_t now = io->now(io->user);

    L->vsyncsSinceClose++;
    if (newFrame) {
        const unsigned v = L->vsyncsSinceClose;

        /* the first close since the reset ends no whole frame: perFrame
           stays (two) until the next */
        if (L->closeSeen) {
            L->perFrame = v < 1 ? 1 : (v > 4 ? 4 : v);
        }
        L->closeSeen = true;
        L->vsyncsSinceClose = 0;
        L->sinceClose = 0;
    }
    if (L->vblanks >= L->n) {
        /* a stall owed this vsync: its vblanks have passed by the last
           step's (due then, so the caller sees how late it is) */
        L->vblanks -= L->n;
        L->debtSteps++;
        return lock_step(L, L->due);
    }
    unsigned atOnce = 0; /* at-once returns this vsync */

    for (;;) {
        unsigned vb = 0;
        const float a = pace_lock_alpha(L->sinceClose + 1, L->n, L->perFrame);

        if (!io->present(io->user, a)) {
            /* nothing presented: the grid */
            return lock_grid(L, io, io->now(io->user));
        }
        now = io->now(io->user);
        L->presents++;
        const uint64_t iv = L->lastReturn != 0 && now > L->lastReturn ? now - L->lastReturn : 0;

        if (io->shown != NULL && !io->shown(io->user)) {
            /* not on the screen (minimised, occluded): not judged, the
               grid; the next interval spans it */
            L->unshown++;
            L->lastReturn = now;
            lock_trace(L, iv, 'n');
            return lock_grid(L, io, now);
        }
        const bool measured = L->onVblank && !L->unjudged;
        const PaceLockVerdict v = pace_lock_judge(L, now, &vb);

        if (v == PACE_LOCK_MISS_AT_ONCE && L->filling && ++atOnce < PACE_LOCK_FILL_PRESENTS) {
            /* the queue is filling: present again, now */
            L->fillPresents++;
            lock_trace(L, iv, 'f');
            continue;
        }
        lock_trace(L, iv,
                   v == PACE_LOCK_MISS_AT_ONCE      ? 'a'
                   : v == PACE_LOCK_MISS_RATE       ? 'r'
                   : v == PACE_LOCK_STALL           ? 's'
                   : v == PACE_LOCK_HIT && measured ? 'h'
                                                    : 'u');
        if (v == PACE_LOCK_MISS_AT_ONCE) {
            L->missesTotal++;
            if (++L->misses >= PACE_LOCK_MISSES) {
                L->dropped = PACE_LOCK_DROP_AT_ONCE;
            }
            return lock_grid(L, io, now);
        }
        if (v == PACE_LOCK_MISS_RATE) {
            /* a window's mean off the refresh: this vsync is still a
               vblank's; a run of such windows drops the lock */
            L->missesTotal++;
            if (L->rateOut >= PACE_LOCK_RATE_WINDOWS) {
                L->dropped = PACE_LOCK_DROP_RATE;
                return lock_grid(L, io, now);
            }
        }
        if (v == PACE_LOCK_HIT || v == PACE_LOCK_MISS_RATE) {
            L->misses = 0;
            L->filling = false; /* the queue is full: an at-once return is a miss */
        }
        L->sinceClose += vb;
        L->vblanks += vb;
        if (L->vblanks >= L->n) {
            L->vblanks -= L->n;
            if (L->vblanks > PACE_LOCK_DEBT_VSYNCS * L->n) {
                /* a long stall: what is past PACE_LOCK_DEBT_VSYNCS
                   vsyncs is dropped, not caught up */
                const unsigned drop = L->vblanks - PACE_LOCK_DEBT_VSYNCS * L->n;

                L->resyncs++;
                L->droppedNs += (uint64_t)drop * L->refreshNs;
                L->vblanks -= drop;
                L->rateClean = false;
            }
            return lock_step(L, now);
        }
    }
}

double pace_lock_nominal_ratio(const PaceLock *L)
{
    if (L->n == 0 || L->refreshNs == 0 || L->periodNs == 0) {
        return 1.0;
    }
    const double r = (double)L->periodNs / ((double)L->n * (double)L->refreshNs);

    return r > 1.02 || r < 0.98 ? 1.0 : r;
}

double pace_lock_rate_ratio(PaceLock *L)
{
    double ratio = 1.0;

    if (L->rateClean && L->rateSteps < PACE_LOCK_RATE_MIN_STEPS) {
        return 0.0; /* too short yet: the block goes on */
    }
    if (L->rateClean && L->rateLast > L->rateFirst && L->periodNs != 0) {
        ratio = (double)L->rateSteps * (double)L->periodNs / (double)(L->rateLast - L->rateFirst);
        if (ratio > 1.02 || ratio < 0.98) {
            ratio = 1.0;
        }
    }
    /* a new block from the last step */
    L->rateFirst = L->rateLast;
    L->rateSteps = 0;
    L->rateClean = true;
    return ratio;
}
