/*
 * port/platform/perf_block.h
 *
 * A 10 s block of the renderer's replays, by phase (window_host.c's second
 * 10 s line), free of SDL and the renderer so a unit test checks it: the
 * sums give the averages, the maxes say which phase made the slowest
 * replay slow, and the replays over a threshold (the refresh: a replay
 * that cannot fit a slot) are counted.  The phases are RdPerfRecord's
 * (port/render/rd.h), in its order.
 */
#ifndef ICO_PLATFORM_PERF_BLOCK_H
#define ICO_PLATFORM_PERF_BLOCK_H

#include <stdbool.h>

enum {
    PERF_PH_INTERP = 0,
    PERF_PH_WAIT,
    PERF_PH_ACQUIRE,
    PERF_PH_UPLOAD,
    PERF_PH_WALK,
    PERF_PH_BIND,
    PERF_PH_SUBMIT,
    PERF_PH_PRESENT,
    PERF_PH_READBACK,
    PERF_PH_FENCE, /* the backend's blocked time on GPU completion (inside wait, readback) */
    PERF_PH_COUNT
};

/* Zero-initialise, or perf_block_reset. */
typedef struct PerfBlock {
    unsigned n;    /* replays added */
    unsigned over; /* of them, over the threshold */
    double total, totalMax;
    double sum[PERF_PH_COUNT], max[PERF_PH_COUNT];
} PerfBlock;

void perf_block_reset(PerfBlock *b);

/* One replay: its phases (ms) and total (ms).  Returns whether the total
 * is over thresholdMs (0: never), counted in over. */
bool perf_block_add(PerfBlock *b, const double ph[PERF_PH_COUNT], double totalMs,
                    double thresholdMs);

/* The longest of the phases that wait or work, as text: "wait 28.1,
 * acquire 0.0, upload 0.4, walk 1.2, bind 0.1, submit 0.1, present 11.0".
 * Returns the characters written (snprintf's: as many as would have been,
 * the text cut at len - 1). */
int perf_block_max_text(const PerfBlock *b, char *buf, unsigned len);

#endif /* ICO_PLATFORM_PERF_BLOCK_H */
