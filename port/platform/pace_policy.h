/*
 * port/platform/pace_policy.h
 *
 * The window's "is a present slow" decision, free of SDL so a
 * unit test checks it. A present with vsync on blocks up to one display
 * refresh, so slow means a cost above max(refresh, period) + period / 2; with
 * an effects program loaded (ReShade, vkBasalt: rhi_injector_name() != NULL)
 * each present also runs the program's passes, so the threshold is
 * max(refresh, period) + refresh + period / 2.
 *
 * The cost is smoothed (the median of the last 8 presents, so one spike
 * changes nothing) and the state has hysteresis: it turns on after 3 presents
 * in a row above the threshold and off when the median falls below 80 % of it.
 */
#ifndef ICO_PLATFORM_PACE_POLICY_H
#define ICO_PLATFORM_PACE_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#define PACE_HIST_LEN 8
#define PACE_ENTER_COUNT 3

/* Zero-initialise before the first call. */
typedef struct PaceHist {
    uint64_t cost[PACE_HIST_LEN];
    unsigned count; /* valid entries, up to PACE_HIST_LEN */
    unsigned next;  /* where the next cost goes */
    unsigned over;  /* consecutive presents with the median above the threshold */
    bool slow;      /* the state last returned */
} PaceHist;

/* The slow threshold in nanoseconds. */
uint64_t pace_slow_threshold(uint64_t refreshNs, uint64_t periodNs, bool injector);

/* Records one present's cost and returns whether presents are slow now. */
bool pace_slow_present(PaceHist *h, uint64_t costNs, uint64_t refreshNs, uint64_t periodNs,
                       bool injector);

/* The least time between two presents while the window presents between
 * game frames.  cap > 0: a frame rate limit, one present per 1/cap s.
 * uncappedVsync: frame rate "uncapped" with vsync on.  Then a sixteenth of
 * a refresh less than one display refresh (refreshNs), in mailbox mode and
 * under FIFO alike.  The gap counts from the start of the last present, and
 * late wake-ups and game steps only ever add to it, so the presents average
 * about one a refresh: under FIFO a present rarely finds the queue full and
 * waits, and in mailbox mode every refresh gets a new picture with few drawn
 * that the display never shows (a gap of exactly one refresh averaged a
 * little more than a refresh, and the display showed some pictures twice).
 * In mailbox mode never fewer than sixty a second whatever refresh the
 * display reports (a 4K screen reported 30 Hz while running at 60).
 * 0 (no gap: back to back) otherwise, i.e. "uncapped" without vsync. */
uint64_t pace_present_gap(int cap, bool uncappedVsync, bool mailbox, uint64_t refreshNs);

/* Whether the window takes "uncapped"'s gap (pace_present_gap's
 * uncappedVsync) rather than the cap's: "uncapped" with vsync on (wanted:
 * the window asked for mailbox, s_pres.mailbox), and a cap equal to the
 * refresh once the vsync lock was dropped (lockDropped) while the swapchain
 * really is in mailbox (mailboxNow: rhi_present_mailbox).  A mailbox
 * present on a timer of exactly one refresh lands at a fixed place against
 * the vblanks; each time a game step delays one past its vblank the
 * display shows the picture before again (the rc2 log after the drop:
 * "0.97 presents a refresh"), for seconds at a time as the 59.94 Hz steps
 * slide across the 60 Hz presents.  A sixteenth of a refresh less gives
 * every refresh a new picture, as at "uncapped".  Under FIFO (D3D12, or no
 * mailbox offered) a present past the refresh would block: the cap's gap. */
bool pace_gap_as_uncapped(bool uncapped, bool lockDropped, bool wanted, bool mailboxNow);

/* True when a numeric frame rate limit (cap > 0) equals the display's rate:
 * within 0.5 Hz (60 on a 59.94 or 60.00 Hz screen, 120 on 119.88 or 120.00),
 * and up to 1.0 Hz above a reported rate of 100 Hz or more that is a whole
 * number: Windows reports whole numbers cut down, so a 143.9 Hz screen reads
 * 143 and a 239.8 Hz one 239, and 144 or 240 equals them.  Below 100 Hz the
 * 0.5 Hz holds: SDL 3.4 turns Windows' 29, 59 and 119 into x / 1.001
 * (src/video/windows/SDL_windowsmodes.c WIN_GetRefreshRate), so a reported
 * whole 59 is a true 59.00 Hz mode, and a cap of 60 locked to it would run
 * the game 1.7 % slow.
 * With vsync on such a limit takes the FIFO present mode and no timer gap
 * (the blocking present holds each present to a vblank): the simulation's
 * 59.94 Hz clock is not the display's, and presents timed from it left some
 * refresh without a new picture (issue 74).  A limit above the refresh keeps
 * mailbox and its own gap, one below keeps its own gap. */
bool pace_cap_equals_refresh(int cap, uint64_t refreshNs);

/* ------------------------------------------------------------------------
 * The vsync lock (issue 74).  With vsync on and the cap equal to the
 * display's rate the present mode is FIFO: a present blocks until a vblank.
 * When the cap is a whole multiple of the simulation's rate (1 % at most
 * off: 60 or 120 on 59.94, 50 or 100 on 50) the DISPLAY is the simulation's
 * clock: each simulated vsync presents exactly n = cap / rate times and the
 * step runs right after the n-th present returns (its vblank), never on a
 * timer of its own.  Two free-running clocks (the simulation's 59.94 Hz grid
 * and a 60.00 Hz display) cannot stay aligned: a present started just before
 * the grid's deadline blocked a whole refresh and ran the step ~16 ms late.
 * The simulation then runs at refresh / n (0.1 % fast on a 60.00 Hz screen;
 * the audio follows, pace_lock_rate_ratio).
 *
 * Each present's alpha is the exact fraction of the frame on the present
 * grid: the k-th vblank since the last frame closed shows
 * k / (n * vsyncs per frame), 1 at most (pace_lock_alpha).
 *
 * The guard: every present's return is judged against the last one
 * (pace_lock_judge).  One that returned in under half a refresh did not
 * wait for a vblank (a compositor or driver that does not block): a miss.
 * But a FIFO present only blocks once the swapchain's queue is full (three
 * images, two frames in flight; NVIDIA's acquire does not wait), so from an
 * empty queue (the lock's start, after a stall, a sleep or a swapchain
 * rebuild) the first presents return at once: while the queue fills
 * (L->filling, until the first hit) an at-once return presents again at
 * once, the same picture, up to PACE_LOCK_FILL_PRESENTS presents in the
 * vsync, before it counts as a miss.  PACE_LOCK_MISSES at-once misses in a
 * row drop the lock (a present that never blocks).
 *
 * The rate: a present's return is not the vblank.  On NVIDIA the replay
 * waits at its start for the GPU to finish the frame two before, and the
 * GPU draws into an image only once the display let go of it, so a return
 * comes the GPU's time and the replay's CPU time after a vblank; when the
 * work changes (a menu closing, the scene drawn again) every return moves
 * with it, and for a few vsyncs each interval is that change longer or
 * shorter than a refresh.  The rc2 guard (the median of the last 8
 * intervals within 2 %) took a rise of the work by 0.5 ms a vsync over 12
 * vsyncs for a display off its rate and dropped the lock in 13 vsyncs
 * (pace_loop_test testNvMenuClose; the v0.7.0 rc2 log on an RTX 3090).
 * The intervals' sum over a run of returns is the last return less the
 * first, so the mean over PACE_LOCK_RATE_WINDOW intervals moves by the
 * change of the work over the window divided by the window (a few ms over
 * 120 refreshes: well inside 2 %), while a display off its rate is off in
 * every interval.  Only intervals between two returns on a vblank are
 * summed (one that ended an interval of half a refresh to 1.5 refreshes;
 * one from the first return, a return after a sleep, an at-once return or
 * a stall counts one vblank and is not summed), none in the first
 * PACE_LOCK_SETTLE intervals after a reset (the queue and the GPU's work
 * settling after a (re)start).  A window's mean outside 2 % of the nominal
 * refresh is a rate miss (the vsync is still stepped on its vblank);
 * PACE_LOCK_RATE_WINDOWS such windows in a row drop the lock, a window
 * inside 2 % clears the count.
 *
 * A present the window reports as not shown (PaceLockIo.shown: minimised,
 * hidden, occluded, a 0x0 surface) is not judged: the vsync runs on the
 * grid, no miss.  An at-once miss times that vsync's step on the
 * simulation's own grid (the last step's due time + the period, as without
 * the lock); a dropped lock sends the window back to the timer and mailbox
 * until the next settings apply.  An interval of 1.5 refreshes or more is a
 * stall (a slow step or replay): the vblanks it spanned count, so the
 * simulation steps for them without presenting (never more than
 * PACE_LOCK_DEBT_VSYNCS vsyncs of them; the rest is dropped and counted),
 * and it is neither a hit nor a miss.  The interval after a sleep is not
 * judged.
 *
 * For the log: the first PACE_LOCK_TRACE_FIRST presents after a reset and
 * the last PACE_LOCK_TRACE_LAST are kept with their interval and what the
 * lock made of it (pace_lock_trace_text).
 */
#define PACE_LOCK_MISSES 3
#define PACE_LOCK_RATE_WINDOW 120
#define PACE_LOCK_RATE_WINDOWS 2
#define PACE_LOCK_SETTLE 60
#define PACE_LOCK_TRACE_FIRST 48
#define PACE_LOCK_TRACE_LAST 16
#define PACE_LOCK_DEBT_VSYNCS 6
#define PACE_LOCK_FILL_PRESENTS 4
/* a measured rate block for the audio needs this many steps */
#define PACE_LOCK_RATE_MIN_STEPS 100

/* why the lock is not held (PaceLock.dropped) */
enum { PACE_LOCK_HELD = 0, PACE_LOCK_DROP_AT_ONCE = 1, PACE_LOCK_DROP_RATE = 2 };

typedef enum PaceLockVerdict {
    PACE_LOCK_HIT,          /* one refresh (half a refresh to 1.5) from a return on a vblank */
    PACE_LOCK_MISS_AT_ONCE, /* returned in under half a refresh */
    PACE_LOCK_MISS_RATE,    /* one refresh that closed a rate window outside 2 % */
    PACE_LOCK_STALL,        /* 1.5 refreshes or more: the vblanks spanned count */
    PACE_LOCK_UNJUDGED      /* the first present, or the first after a sleep */
} PaceLockVerdict;

/* Zero-initialise, then pace_lock_reset. */
typedef struct PaceLock {
    unsigned n;          /* presents a simulated vsync (pace_lock_presents) */
    uint64_t refreshNs;  /* the display's nominal refresh */
    uint64_t periodNs;   /* the simulation's vsync period (the grid's) */
    uint64_t due;        /* when the last step was due */
    uint64_t lastReturn; /* the last present's return (0: none yet) */
    bool unjudged;       /* the next interval spans a sleep: not judged */
    unsigned vblanks;    /* vblanks since the last step (n or more: steps owed) */
    unsigned sinceClose; /* vblanks since the last game frame closed */
    unsigned perFrame;   /* simulated vsyncs per game frame (the last two closes'; 1..4) */
    unsigned vsyncsSinceClose;
    bool closeSeen; /* a frame closed since the reset (perFrame is measured from the next) */
    bool filling;   /* the swapchain's queue may be filling: at-once returns present again */
    bool onVblank;  /* the last return ended an interval of about a refresh (it waited for
                         a vblank): only an interval from such a return is a measure */
    /* the rate window: summed intervals between returns on a vblank */
    uint64_t rateSumNs;
    unsigned rateCount;   /* intervals in the window */
    unsigned settled;     /* vblank intervals since the reset, up to PACE_LOCK_SETTLE */
    unsigned rateOut;     /* windows in a row outside 2 % */
    uint64_t rateMeanNs;  /* the last window's mean (0: none yet) */
    unsigned misses;      /* at-once misses in a row */
    unsigned missesTotal; /* at-once and rate misses since the reset */
    int dropped;          /* PACE_LOCK_HELD or why it was dropped */
    /* counts since pace_lock_reset */
    unsigned presents, steps, debtSteps, gridSteps, resyncs;
    unsigned fillPresents; /* at-once presents made again while the queue filled */
    unsigned unshown;      /* vsyncs whose present the window reported not shown */
    uint64_t droppedNs;    /* display time dropped (more debt than PACE_LOCK_DEBT_VSYNCS) */
    /* the rate block (pace_lock_rate_ratio) */
    uint64_t rateFirst, rateLast;
    unsigned rateSteps;
    bool rateClean; /* no miss, nothing dropped, nothing unpresented since rateFirst */
    /* the log's trace: each present's interval from the return before
       (0: none) and a letter (pace_lock_trace_text) */
    uint32_t traceFirstUs[PACE_LOCK_TRACE_FIRST], traceLastUs[PACE_LOCK_TRACE_LAST];
    char traceFirstV[PACE_LOCK_TRACE_FIRST], traceLastV[PACE_LOCK_TRACE_LAST];
    unsigned traceFirstCount, traceLastCount, traceLastNext;
    bool traceLogged; /* the caller logged the first ones */
} PaceLock;

/* n for a cap equal to the refresh: round(cap * periodNs / 1e9) when the cap
 * is within 1 % of n simulated vsync rates, else 0 (144 or 75 on 59.94, 60
 * on 50: no whole multiple; the window steps on the simulation's grid). */
unsigned pace_lock_presents(int cap, uint64_t periodNs);

/* An interval within 2 % of the refresh: |interval - refresh| < refresh / 50. */
bool pace_lock_rate_ok(uint64_t intervalNs, uint64_t refreshNs);

/* The alpha of a present that lands k vblanks after the last frame closed:
 * k / (n * perFrame), 1 at most (0 when n or perFrame is 0). */
float pace_lock_alpha(unsigned k, unsigned n, unsigned perFrame);

/* A new lock (the first locked vsync, a new n or refresh, the window back
 * from the background): the history and counts cleared; the last step was
 * due at nowNs. */
void pace_lock_reset(PaceLock *L, unsigned n, uint64_t refreshNs, uint64_t periodNs,
                     uint64_t nowNs);

/* The verdict on a present that returned at returnNs (pure but for L's
 * history: lastReturn, the intervals, unjudged).  *vblanks: the display
 * refreshes it stands for (1, a stall's count, 0 at once). */
PaceLockVerdict pace_lock_judge(PaceLock *L, uint64_t returnNs, unsigned *vblanks);

/* The trace as text, "16.67h 4.10f ..." (ms from the return before, then
 * h one refresh measured, u one refresh not measured (the first return,
 * after a sleep, or from a return not on a vblank), f at once while the
 * queue filled (presented again), a at once (a miss), s a stall, r one
 * refresh that closed a rate window outside 2 %, n not shown): the first
 * PACE_LOCK_TRACE_FIRST presents since the reset (last false) or the last
 * PACE_LOCK_TRACE_LAST, oldest first.  Returns the presents written. */
unsigned pace_lock_trace_text(const PaceLock *L, bool last, char *buf, unsigned len);

/* What the loop needs from the window (a fake one in pace_loop_test). */
typedef struct PaceLockIo {
    void *user;
    uint64_t (*now)(void *user);
    /* replays and presents at alpha; blocks as the present mode does;
       false: nothing presented (a movie on the output, nothing closed yet) */
    bool (*present)(void *user, float alpha);
    void (*sleepUntil)(void *user, uint64_t ns);
    /* after a present that returned true: false when it did not reach the
       display (the window minimised, hidden or occluded, a D3D12 present
       that reported DXGI_STATUS_OCCLUDED, a Vulkan surface at 0x0): it is
       not judged.  NULL: always shown */
    bool (*shown)(void *user);
} PaceLockIo;

/* One simulated vsync under the lock (L->n > 0, L->dropped == 0): presents
 * until the n-th present of the vsync returns, or steps at once for a vblank
 * a stall owed; on a miss or a present that did nothing, sleeps to the
 * grid's deadline.  newFrame: a game frame closed in the step before this
 * call.  Returns when the step that follows was due (the n-th present's
 * return under the lock); the caller steps at once.  L->dropped is set when
 * this vsync's miss was the PACE_LOCK_MISSES-th in a row. */
uint64_t pace_lock_vsync(PaceLock *L, const PaceLockIo *io, bool newFrame);

/* The simulated vsync rate under the lock as the display's nominal
 * refresh gives it, over the simulation's: periodNs / (n * refreshNs), 1.001
 * for one present a vsync on a 60.00 Hz screen under 59.94 (1.0 when more
 * than 2 % from 1, or n or the refresh is 0).  The audio's ratio from the
 * lock's start, until the first measured block (pace_lock_rate_ratio), so
 * its queue does not grow by 0.1 % over the first 10 s. */
double pace_lock_nominal_ratio(const PaceLock *L);

/* The simulated vsync rate measured over the steps since the last call,
 * over the nominal (1e9 / periodNs): 1.001 on a 60.00 Hz display under a
 * 59.94 Hz simulation.  1.0 when the block was not steady (a miss, a
 * present that did nothing, lag dropped) or the ratio is more than 2 % from
 * 1; either way a new block starts.  0 while a steady block has fewer than
 * PACE_LOCK_RATE_MIN_STEPS steps: it goes on (the caller keeps its ratio). */
double pace_lock_rate_ratio(PaceLock *L);

/* Without the lock, under FIFO with a refresh shorter than the
 * simulation's period (a cap of 144 on 59.94, 60 on PAL's 50): whether the
 * window holds this vsync's next present back and sleeps to the deadline
 * because it would return on a vblank past it (a FIFO present returns on
 * the first vblank after it is made, on the last return's grid of
 * refreshes; one started just before the deadline ran the step a refresh
 * late, issue 74).  Never a frame that is not shown yet once the deadline
 * has passed (the game behind: one present per new frame), and a new frame
 * is held back at most once (frameHeld: this frame was held at the last
 * vsync), so a game whose step is about a period long still shows its
 * frames.  lastReturn 0: no present yet, nothing held. */
bool pace_fifo_hold(uint64_t nowNs, uint64_t lastReturnNs, uint64_t refreshNs, uint64_t deadlineNs,
                    bool frameShown, bool frameHeld);

/* The limiter's grid: when the present after one that started at startNs is
 * due.  nextPresent is when that one was due (0: none yet).  The result is
 * nextPresent + gap, so a late wake-up or a present held behind a game step
 * does not push every later present back; when startNs is more than a gap
 * past nextPresent (a stall) the grid restarts at startNs + gap. */
uint64_t pace_next_present(uint64_t nextPresent, uint64_t startNs, uint64_t gap);

/* Resolution "auto" (video_options.h ICO_RES_AUTO).  The window
 * feeds each present's cost (the GPU time of a replay when the backend has
 * timestamps, else its CPU time without the acquire and the present, which
 * wait for the display) into a PaceSamples; every PACE_AUTO_WINDOW_NS it asks
 * pace_auto_resolution_step for the scene scale and starts a new window.
 *
 * The rule: when the median cost over the window (at least
 * PACE_AUTO_WINDOW_NS of samples, at least PACE_AUTO_MIN_SAMPLES of them)
 * is above 70 % of the frame budget (1/60 s, or the frame rate cap's
 * period), the scale steps down once: the window's own size (scale 0) goes
 * to the largest of 3x, 2x, 1x below the window's scale, Nx to (N-1)x and
 * anything above 3x straight to 3x;
 * never below 1x, never back up, and at most once per PACE_AUTO_WINDOW_NS
 * (lastStepNs). */
#define PACE_AUTO_SAMPLES 256
#define PACE_AUTO_MIN_SAMPLES 8
#define PACE_AUTO_WINDOW_NS 2000000000ull

/* Zero-initialise (or pace_samples_reset) before the first sample. */
typedef struct PaceSamples {
    uint64_t cost[PACE_AUTO_SAMPLES]; /* a ring: the newest PACE_AUTO_SAMPLES */
    unsigned count;                   /* valid entries, up to PACE_AUTO_SAMPLES */
    unsigned next;                    /* where the next cost goes */
    uint64_t firstNs;                 /* the window's start (0: its first sample's time) */
    uint64_t lastNs;                  /* the newest sample's time */
    uint64_t budgetNs;                /* the frame budget the costs are held against */
    uint64_t lastStepNs;              /* when the scale last stepped (0: never) */
} PaceSamples;

/* A new window starting at nowNs: the samples dropped; budgetNs and
   lastStepNs kept. */
void pace_samples_reset(PaceSamples *s, uint64_t nowNs);

/* One present's cost at time nowNs (any monotonic clock in ns, above 0). */
void pace_samples_add(PaceSamples *s, uint64_t nowNs, uint64_t costNs);

/* The median of the samples (0 with none). */
uint64_t pace_samples_median(const PaceSamples *s);

/* The scene scale resolution "auto" should have: currentScale (0 = the
 * window's size, else N) or the next step down.  windowScale is the
 * window's size against the game's 1x (the presentation box's height over
 * 448), to know which step is below the window's own. */
int pace_auto_resolution_step(const PaceSamples *s, int currentScale, float windowScale);

#endif /* ICO_PLATFORM_PACE_POLICY_H */
