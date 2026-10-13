/*
 * port/platform/perf_block.c
 *
 * A block of replays by phase (perf_block.h).
 */
#include "perf_block.h"

#include <stdio.h>
#include <string.h>

void perf_block_reset(PerfBlock *b)
{
    memset(b, 0, sizeof(*b));
}

bool perf_block_add(PerfBlock *b, const double ph[PERF_PH_COUNT], double totalMs,
                    double thresholdMs)
{
    const bool over = thresholdMs > 0.0 && totalMs > thresholdMs;

    b->n++;
    b->total += totalMs;
    if (totalMs > b->totalMax) {
        b->totalMax = totalMs;
    }
    for (int i = 0; i < PERF_PH_COUNT; i++) {
        b->sum[i] += ph[i];
        if (ph[i] > b->max[i]) {
            b->max[i] = ph[i];
        }
    }
    b->over += over;
    return over;
}

int perf_block_max_text(const PerfBlock *b, char *buf, unsigned len)
{
    return snprintf(buf, len,
                    "wait %.1f, acquire %.1f, upload %.1f, walk %.1f, bind %.1f, submit %.1f, "
                    "present %.1f",
                    b->max[PERF_PH_WAIT], b->max[PERF_PH_ACQUIRE], b->max[PERF_PH_UPLOAD],
                    b->max[PERF_PH_WALK], b->max[PERF_PH_BIND], b->max[PERF_PH_SUBMIT],
                    b->max[PERF_PH_PRESENT]);
}
