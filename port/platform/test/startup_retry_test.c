/*
 * port/platform/test/startup_retry_test.c
 *
 * The steps after a graphics driver crash while building a pipeline or a
 * device loss (startup_retry.h):
 *   - startup_retry_next over both reasons, every step, the platform able
 *     to start the game again or not, cache loaded or not, D3D12 in the
 *     build or not, the backend and the blending named by the player or
 *     not, the run's blend already fixed or not, on Vulkan and on D3D12,
 *     against the crash order cache, d3d12, lazy, none and the loss order
 *     blend, backend, none; the reason is never empty and a step only
 *     grows (every chain ends);
 *   - a crash chain from the first run to the box, as the handler walks it;
 *   - the loss chains on Windows from Vulkan and from D3D12, on Linux and
 *     macOS, on Android and iOS, with blend= or backend= in the settings,
 *     and a loss after a crash restart;
 *   - the variable's value written and read back, and bad values refused;
 *   - the relaunch environment: the POSIX pointer list and the Windows
 *     block, each without an inherited ICO_STARTUP_RETRY and with the new
 *     one (a crash step and a loss step), and too-small buffers refused.
 */
#include <stdio.h>
#include <string.h>
#include "startup_retry.h"
#include "../../test/ico_check.h"

/* the crash order, written out independently of startup_retry.c */
static StartupRetry expected_crash(StartupRetry step, bool cache, bool d3d12, bool expl,
                                   const char *backend)
{
    const bool onD3d12 = strcmp(backend, "d3d12") == 0;
    if (step == STARTUP_RETRY_LOSS_BLEND || step == STARTUP_RETRY_LOSS_BACKEND) {
        return STARTUP_RETRY_NONE; /* a crash after a loss restart: the box */
    }
    if (step == STARTUP_RETRY_NONE && cache) {
        return STARTUP_RETRY_CACHE;
    }
    if ((step == STARTUP_RETRY_NONE || step == STARTUP_RETRY_CACHE) && d3d12 && !expl && !onD3d12) {
        return STARTUP_RETRY_D3D12;
    }
    if (step != STARTUP_RETRY_LAZY) {
        return STARTUP_RETRY_LAZY;
    }
    return STARTUP_RETRY_NONE;
}

/* the loss order: blend (same backend, fixed blend) unless the run was
   already fixed or the player chose the blending; then backend (the other
   one, fixed blend) in a two-backend build when the player chose neither
   the backend nor a blending other than fixed; then the box */
static StartupRetry expected_loss(StartupRetry step, bool d3d12, bool expl, bool blendExpl,
                                  bool wasFixed)
{
    const bool beforeBlend = step != STARTUP_RETRY_LOSS_BLEND && step != STARTUP_RETRY_LOSS_BACKEND;
    if (beforeBlend && !wasFixed && !blendExpl) {
        return STARTUP_RETRY_LOSS_BLEND;
    }
    if (step != STARTUP_RETRY_LOSS_BACKEND && d3d12 && !expl && (!blendExpl || wasFixed)) {
        return STARTUP_RETRY_LOSS_BACKEND;
    }
    return STARTUP_RETRY_NONE;
}

/* every reason, step, platform (relaunch or not), cache, D3D12 in the
   build, backend and blend named or not, blend already fixed or not, on
   Vulkan and D3D12 */
static void test_table(void)
{
    static const char *const backends[] = {"vulkan", "d3d12"};
    int combos = 0;
    for (int why = STARTUP_RETRY_CRASH; why <= STARTUP_RETRY_LOSS; why++) {
        for (int step = STARTUP_RETRY_NONE; step <= STARTUP_RETRY_LOSS_BACKEND; step++) {
            for (int bits = 0; bits < 64; bits++) {
                for (int b = 0; b < 2; b++) {
                    const bool cache = bits & 1, d3d12 = bits & 2, expl = bits & 4,
                               blendExpl = bits & 8, wasFixed = bits & 16, relaunch = bits & 32;
                    const StartupRetryRun run = {
                        .why = (StartupRetryWhy)why,
                        .step = (StartupRetry)step,
                        .canRelaunch = relaunch,
                        .cacheWasLoaded = cache,
                        .haveD3d12 = d3d12,
                        .backendExplicit = expl,
                        .blendExplicit = blendExpl,
                        .blendWasFixed = wasFixed,
                        .backendName = backends[b],
                    };
                    char reason[200];
                    reason[0] = '\0';
                    const StartupRetry got = startup_retry_next(&run, reason, sizeof reason);
                    StartupRetry want = STARTUP_RETRY_NONE;
                    if (relaunch) {
                        want = why == STARTUP_RETRY_CRASH
                                   ? expected_crash((StartupRetry)step, cache, d3d12, expl,
                                                    backends[b])
                                   : expected_loss((StartupRetry)step, d3d12, expl, blendExpl,
                                                   wasFixed);
                    }
                    CHECK(got == want,
                          "why %d step %d cache %d d3d12 %d explicit %d blend explicit %d "
                          "fixed %d relaunch %d %s: got %d, want %d",
                          why, step, cache, d3d12, expl, blendExpl, wasFixed, relaunch, backends[b],
                          (int)got, (int)want);
                    CHECK(reason[0] != '\0', "why %d step %d: empty reason", why, step);
                    /* a loss step only grows, and never comes from a crash */
                    if (got != STARTUP_RETRY_NONE) {
                        CHECK(got > (StartupRetry)step, "step %d went back to %d", step, (int)got);
                        CHECK(startup_retry_is_loss(got) == (why == STARTUP_RETRY_LOSS));
                    }
                    combos++;
                }
            }
        }
    }
    CHECK(combos == 2 * 6 * 64 * 2);
    /* no name means the default backend; a NULL reason is allowed */
    {
        StartupRetryRun run = {.canRelaunch = true, .haveD3d12 = true};
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_D3D12);
        /* names in any case */
        run.backendName = "D3D12";
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LAZY);
        run.why = STARTUP_RETRY_LOSS;
        run.blendWasFixed = true;
        char v[32];
        CHECK(startup_retry_next(&run, NULL, 0) == STARTUP_RETRY_LOSS_BACKEND);
        CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BACKEND, "D3D12", v, sizeof v) == 0);
        CHECK_STR(v, "backend:vulkan");
    }
}

/* one crash-chain step from the run described by the arguments */
static StartupRetry crash_step(StartupRetry step, bool cache, bool d3d12, bool expl,
                               const char *backend)
{
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_CRASH,
        .step = step,
        .canRelaunch = true,
        .cacheWasLoaded = cache,
        .haveD3d12 = d3d12,
        .backendExplicit = expl,
        .backendName = backend,
    };
    return startup_retry_next(&run, NULL, 0);
}

/* the handler's walk on a Windows build with a loaded cache: cache,
 * d3d12, lazy, then nothing; the reporter's case (no cache) starts at
 * d3d12; an explicit backend goes straight to lazy */
static void test_chain(void)
{
    char v[32], be[16];
    StartupRetry s = STARTUP_RETRY_NONE, p;
    const char *backend = "vulkan";
    static const StartupRetry walk[] = {STARTUP_RETRY_CACHE, STARTUP_RETRY_D3D12,
                                        STARTUP_RETRY_LAZY, STARTUP_RETRY_NONE};
    static const char *const values[] = {"cache:vulkan", "d3d12:d3d12", "lazy:d3d12"};
    bool cache = true;
    for (int i = 0; i < 4; i++) {
        s = crash_step(s, cache, true, false, backend);
        CHECK(s == walk[i], "chain step %d: %d", i, (int)s);
        if (s == STARTUP_RETRY_NONE) {
            CHECK(startup_retry_value(s, backend, v, sizeof v) == -1);
            break;
        }
        CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
        CHECK_STR(v, values[i]);
        /* the relaunched run */
        CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0);
        CHECK(p == s);
        backend = be[0] == 'd' ? "d3d12" : "vulkan";
        cache = false; /* deleted, or another backend */
    }
    CHECK(crash_step(STARTUP_RETRY_NONE, false, true, false, "vulkan") == STARTUP_RETRY_D3D12);
    CHECK(crash_step(STARTUP_RETRY_NONE, false, true, true, "vulkan") == STARTUP_RETRY_LAZY);
    CHECK(crash_step(STARTUP_RETRY_NONE, false, false, false, "vulkan") == STARTUP_RETRY_LAZY);
}

/* one loss-chain step */
static StartupRetry loss_step(StartupRetry step, bool d3d12, bool expl, bool blendExpl,
                              bool wasFixed, const char *backend, bool relaunch)
{
    const StartupRetryRun run = {
        .why = STARTUP_RETRY_LOSS,
        .step = step,
        .canRelaunch = relaunch,
        .haveD3d12 = d3d12,
        .backendExplicit = expl,
        .blendExplicit = blendExpl,
        .blendWasFixed = wasFixed,
        .backendName = backend,
    };
    char reason[200];
    reason[0] = '\0';
    const StartupRetry next = startup_retry_next(&run, reason, sizeof reason);
    CHECK(reason[0] != '\0');
    return next;
}

/* the walks after a device loss, as window_host.c and diag_host.c take
   them, each value written and read back as the relaunched run reads it:
   Windows on Vulkan (the reporter's v0.7.2 run): blend:vulkan, then
   backend:d3d12, then the box; Windows on D3D12 (the v0.7.4 run): then
   backend:vulkan; Linux and macOS (no D3D12): blend:vulkan, then the box;
   Android and iOS: the box at once */
static void test_loss_chain(void)
{
    static const struct {
        bool d3d12;
        const char *first;
        const char *values[2];
    } walks[] = {
        {true, "vulkan", {"blend:vulkan", "backend:d3d12"}},
        {true, "d3d12", {"blend:d3d12", "backend:vulkan"}},
        {false, "vulkan", {"blend:vulkan", NULL}},
    };

    for (size_t w = 0; w < sizeof walks / sizeof walks[0]; w++) {
        StartupRetry s = STARTUP_RETRY_NONE, p;
        char v[32], be[16];
        const char *backend = walks[w].first;
        bool fixed = false;
        int i = 0;
        for (;; i++) {
            s = loss_step(s, walks[w].d3d12, false, false, fixed, backend, true);
            if (s == STARTUP_RETRY_NONE) {
                break;
            }
            CHECK(i < 2 && walks[w].values[i] != NULL, "walk %zu: a step too many", w);
            if (i >= 2 || walks[w].values[i] == NULL) {
                break;
            }
            CHECK(startup_retry_value(s, backend, v, sizeof v) == 0);
            CHECK_STR(v, walks[w].values[i]);
            CHECK(startup_retry_value_is_loss(v));
            CHECK(startup_retry_parse(v, &p, be, sizeof be) == 0 && p == s);
            backend = strcmp(be, "d3d12") == 0 ? "d3d12" : "vulkan";
            fixed = true; /* the loss steps draw with the fixed-function blend */
        }
        CHECK(i == (walks[w].values[1] != NULL ? 2 : 1), "walk %zu: %d steps", w, i);
    }
    /* Android and iOS: no relaunch, the box */
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, false, false, "vulkan", false) ==
          STARTUP_RETRY_NONE);
    /* blend=fixed in the ini (the run already drew with it): straight to
       the other backend; without D3D12, the box */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, false, true, true, "vulkan", true) ==
          STARTUP_RETRY_LOSS_BACKEND);
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, true, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* the device had no exact blend (already fixed): no blend step */
    CHECK(loss_step(STARTUP_RETRY_NONE, false, false, false, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* a blending other than fixed named by the player: neither step */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, false, true, false, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* backend= named: the blend step, never the other backend */
    CHECK(loss_step(STARTUP_RETRY_NONE, true, true, false, false, "vulkan", true) ==
          STARTUP_RETRY_LOSS_BLEND);
    CHECK(loss_step(STARTUP_RETRY_LOSS_BLEND, true, true, false, true, "vulkan", true) ==
          STARTUP_RETRY_NONE);
    /* a loss in a run started after a driver crash (lazy on D3D12) still
       gets the loss steps; a crash in a loss step's run gets the box */
    CHECK(loss_step(STARTUP_RETRY_LAZY, true, false, false, false, "d3d12", true) ==
          STARTUP_RETRY_LOSS_BLEND);
    CHECK(crash_step(STARTUP_RETRY_LOSS_BLEND, true, true, false, "vulkan") == STARTUP_RETRY_NONE);
    CHECK(crash_step(STARTUP_RETRY_LOSS_BACKEND, false, false, false, "d3d12") ==
          STARTUP_RETRY_NONE);
}

static void test_values(void)
{
    char v[32], be[16];
    StartupRetry s;
    CHECK(startup_retry_value(STARTUP_RETRY_LAZY, NULL, v, sizeof v) == 0);
    CHECK_STR(v, "lazy:vulkan");
    CHECK(startup_retry_value(STARTUP_RETRY_CACHE, "vulkan", v, 8) == -1);
    CHECK(startup_retry_parse("lazy:vulkan", &s, be, sizeof be) == 0 && s == STARTUP_RETRY_LAZY);
    CHECK_STR(be, "vulkan");
    CHECK(startup_retry_parse("cache:vulkan", &s, be, sizeof be) == 0 && s == STARTUP_RETRY_CACHE);
    CHECK(startup_retry_parse("blend:vulkan", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_LOSS_BLEND);
    CHECK(startup_retry_parse("backend:d3d12", &s, be, sizeof be) == 0 &&
          s == STARTUP_RETRY_LOSS_BACKEND);
    CHECK_STR(be, "d3d12");
    CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BLEND, "d3d12", v, sizeof v) == 0);
    CHECK_STR(v, "blend:d3d12");
    CHECK(startup_retry_value(STARTUP_RETRY_LOSS_BACKEND, NULL, v, sizeof v) == 0);
    CHECK_STR(v, "backend:d3d12");
    /* the loss values, and only they (host_config.c's blend) */
    CHECK(startup_retry_value_is_loss("blend:vulkan"));
    CHECK(startup_retry_value_is_loss("backend:vulkan"));
    CHECK(!startup_retry_value_is_loss("lazy:vulkan"));
    CHECK(!startup_retry_value_is_loss("d3d12:d3d12"));
    CHECK(!startup_retry_value_is_loss("blend"));
    CHECK(!startup_retry_value_is_loss("backen:d3d12"));
    CHECK(!startup_retry_value_is_loss(""));
    CHECK(!startup_retry_value_is_loss(NULL));
    static const char *const bad[] = {"",
                                      "lazy",
                                      "lazy:",
                                      ":vulkan",
                                      "fast:vulkan",
                                      "lazyx:vulkan",
                                      "laz:vulkan",
                                      "blendx:vulkan",
                                      "Blend:vulkan",
                                      "lazy:Vulkan",
                                      "lazy:vul kan",
                                      "lazy:aaaaaaaaaaaaaaaaaaaaaaaa"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        s = STARTUP_RETRY_LAZY;
        CHECK(startup_retry_parse(bad[i], &s, be, sizeof be) == -1, "accepted \"%s\"", bad[i]);
        CHECK(s == STARTUP_RETRY_NONE);
    }
    CHECK(startup_retry_parse(NULL, &s, be, sizeof be) == -1);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_NONE), "");
    CHECK(startup_retry_what(STARTUP_RETRY_CACHE)[0] != '\0');
    CHECK(startup_retry_what(STARTUP_RETRY_D3D12)[0] != '\0');
    CHECK(startup_retry_what(STARTUP_RETRY_LAZY)[0] != '\0');
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_BLEND), "fresh launch") != NULL);
    CHECK(strstr(startup_retry_what(STARTUP_RETRY_LOSS_BACKEND), "fresh launch") != NULL);
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_BLEND), "blend");
    CHECK_STR(startup_retry_name(STARTUP_RETRY_LOSS_BACKEND), "backend");
}

static void test_envp(void)
{
    char *env[] = {"HOME=/home/x",
                   "ICO_STARTUP_RETRY=lazy:vulkan",
                   "PATH=/bin",
                   "ico_startup_retry=cache:vulkan",
                   "ICO_STARTUP_RETRYX=1",
                   NULL};
    const char *out[8];
    int n = startup_retry_envp(env, "ICO_STARTUP_RETRY=d3d12:d3d12", out, 8);
    CHECK(n == 4);
    CHECK_STR(out[0], "HOME=/home/x");
    CHECK_STR(out[1], "PATH=/bin");
    CHECK_STR(out[2], "ICO_STARTUP_RETRYX=1");
    CHECK_STR(out[3], "ICO_STARTUP_RETRY=d3d12:d3d12");
    CHECK(out[4] == NULL);
    CHECK(startup_retry_envp(env, NULL, out, 8) == 3 && out[3] == NULL);
    CHECK(startup_retry_envp(env, "X=1", out, 4) == -1);
    CHECK(startup_retry_envp(env, "X=1", out, 5) == 4);
    CHECK(startup_retry_envp(NULL, NULL, out, 1) == 0 && out[0] == NULL);
    /* the relaunch after a loss: the inherited step replaced by the loss
       step, the rest (an ICO_RD_BLEND the player set among it) kept */
    char *env2[] = {"ICO_RD_BLEND=fixed", "ICO_STARTUP_RETRY=blend:vulkan", "HOME=/h", NULL};
    n = startup_retry_envp(env2, "ICO_STARTUP_RETRY=backend:d3d12", out, 8);
    CHECK(n == 3);
    CHECK_STR(out[0], "ICO_RD_BLEND=fixed");
    CHECK_STR(out[1], "HOME=/h");
    CHECK_STR(out[2], "ICO_STARTUP_RETRY=backend:d3d12");
    CHECK(out[3] == NULL);
}

static void test_block(void)
{
    static const wchar_t in[] = L"=C:=C:\\x\0A=1\0Ico_Startup_Retry=lazy:vulkan\0B=2\0";
    wchar_t out[64];
    long used = startup_retry_block_copy(in, out, 64);
    static const wchar_t want[] = L"=C:=C:\\x\0A=1\0B=2\0ICO_STARTUP_RETRY=lazy:d3d12\0";
    CHECK(used == 17, "used %ld", used);
    CHECK(startup_retry_block_finish(out, (size_t)used, 64, "lazy:d3d12") == 0);
    CHECK(memcmp(out, want, sizeof want) == 0); /* sizeof: the final NUL too */
    CHECK(startup_retry_block_copy(in, out, 10) == -1);
    CHECK(startup_retry_block_finish(out, (size_t)used, (size_t)used + 20, "lazy:d3d12") == -1);
    CHECK(startup_retry_block_copy(L"", out, 4) == 0);
    /* the loss value in the Windows block */
    static const wchar_t in2[] = L"A=1\0ICO_STARTUP_RETRY=lazy:d3d12\0";
    static const wchar_t want2[] = L"A=1\0ICO_STARTUP_RETRY=blend:d3d12\0";
    used = startup_retry_block_copy(in2, out, 64);
    CHECK(used == 4, "used %ld", used);
    CHECK(startup_retry_block_finish(out, (size_t)used, 64, "blend:d3d12") == 0);
    CHECK(memcmp(out, want2, sizeof want2) == 0);
}

int main(void)
{
    test_table();
    test_chain();
    test_loss_chain();
    test_values();
    test_envp();
    test_block();
    return ico_check_summary("startup_retry");
}
