/*
 * port/platform/test/perf_block_test.c
 *
 * A block of replays by phase (perf_block.h): the sums and maxes, the
 * replays over the threshold (none at threshold 0), the longest-phase text,
 * a reset.
 */
#include <stdio.h>
#include <string.h>
#include "perf_block.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int near(double a, double b)
{
    const double d = a - b;

    return d < 1e-9 && d > -1e-9;
}

int main(void)
{
    PerfBlock b;
    double ph[PERF_PH_COUNT];
    char text[160];

    perf_block_reset(&b);
    /* three replays: 10 ms, 28 ms (a wait of 20), 12 ms (a present of 11) */
    memset(ph, 0, sizeof ph);
    ph[PERF_PH_WAIT] = 1.0;
    ph[PERF_PH_WALK] = 2.0;
    ph[PERF_PH_PRESENT] = 7.0;
    CHECK(!perf_block_add(&b, ph, 10.0, 16.68));
    ph[PERF_PH_WAIT] = 20.0;
    ph[PERF_PH_FENCE] = 19.5;
    CHECK(perf_block_add(&b, ph, 28.0, 16.68));
    ph[PERF_PH_WAIT] = 0.5;
    ph[PERF_PH_FENCE] = 0.0;
    ph[PERF_PH_PRESENT] = 11.0;
    ph[PERF_PH_UPLOAD] = 0.4;
    CHECK(!perf_block_add(&b, ph, 12.0, 16.68));
    CHECK(b.n == 3 && b.over == 1);
    CHECK(near(b.total, 50.0) && near(b.totalMax, 28.0));
    CHECK(near(b.sum[PERF_PH_WAIT], 21.5) && near(b.max[PERF_PH_WAIT], 20.0));
    CHECK(near(b.sum[PERF_PH_PRESENT], 25.0) && near(b.max[PERF_PH_PRESENT], 11.0));
    CHECK(near(b.sum[PERF_PH_FENCE], 19.5) && near(b.max[PERF_PH_FENCE], 19.5));
    CHECK(near(b.sum[PERF_PH_WALK], 6.0) && near(b.max[PERF_PH_WALK], 2.0));
    CHECK(near(b.max[PERF_PH_INTERP], 0.0) && near(b.max[PERF_PH_READBACK], 0.0));

    perf_block_max_text(&b, text, sizeof text);
    printf("%s\n", text);
    CHECK(strcmp(text, "wait 20.0, acquire 0.0, upload 0.4, walk 2.0, bind 0.0, submit 0.0, "
                       "present 11.0") == 0);
    /* cut, terminated */
    {
        char small[8];
        const int n = perf_block_max_text(&b, small, sizeof small);

        CHECK(n > 7 && strlen(small) == 7 && strncmp(small, "wait 20", 7) == 0);
    }
    /* threshold 0: never over; equal to it: not over */
    CHECK(!perf_block_add(&b, ph, 100.0, 0.0));
    CHECK(!perf_block_add(&b, ph, 16.68, 16.68));
    CHECK(b.n == 5 && b.over == 1 && near(b.totalMax, 100.0));

    perf_block_reset(&b);
    CHECK(b.n == 0 && b.over == 0 && near(b.total, 0.0) && near(b.totalMax, 0.0));
    for (int i = 0; i < PERF_PH_COUNT; i++) {
        CHECK(near(b.sum[i], 0.0) && near(b.max[i], 0.0));
    }

    if (fails) {
        printf("perf_block_test: %d failures\n", fails);
        return 1;
    }
    printf("perf_block_test: ok\n");
    return 0;
}
