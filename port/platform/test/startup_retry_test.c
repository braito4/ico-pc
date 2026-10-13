/*
 * port/platform/test/startup_retry_test.c
 *
 * The steps after a graphics driver crash while building a pipeline
 * (startup_retry.h):
 *   - startup_retry_next over every step, cache loaded or not, D3D12 in the
 *     build or not, the backend named by the player or not, on Vulkan and
 *     on D3D12, against the order cache, d3d12, lazy, none; the reason is
 *     never empty;
 *   - a crash chain from the first run to the box, as the handler walks it;
 *   - the variable's value written and read back, and bad values refused;
 *   - the relaunch environment: the POSIX pointer list and the Windows
 *     block, each without an inherited ICO_STARTUP_RETRY and with the new
 *     one, and too-small buffers refused.
 */
#include <stdio.h>
#include <string.h>
#include "startup_retry.h"
#include "../../test/ico_check.h"

/* the order, written out independently of startup_retry.c */
static StartupRetry expected(StartupRetry step, bool cache, bool d3d12, bool expl,
                             const char *backend)
{
    const bool onD3d12 = strcmp(backend, "d3d12") == 0;
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

static void test_table(void)
{
    static const char *const backends[] = {"vulkan", "d3d12"};
    int combos = 0;
    for (int step = STARTUP_RETRY_NONE; step <= STARTUP_RETRY_LAZY; step++) {
        for (int bits = 0; bits < 8; bits++) {
            for (int b = 0; b < 2; b++) {
                const bool cache = bits & 1, d3d12 = bits & 2, expl = bits & 4;
                char reason[200];
                reason[0] = '\0';
                const StartupRetry got = startup_retry_next((StartupRetry)step, cache, d3d12, expl,
                                                            backends[b], reason, sizeof reason);
                const StartupRetry want =
                    expected((StartupRetry)step, cache, d3d12, expl, backends[b]);
                CHECK(got == want, "step %d cache %d d3d12 %d explicit %d %s: got %d, want %d",
                      step, cache, d3d12, expl, backends[b], (int)got, (int)want);
                CHECK(reason[0] != '\0', "step %d: empty reason", step);
                combos++;
            }
        }
    }
    CHECK(combos == 64);
    /* no name means the default backend; a NULL reason is allowed */
    CHECK(startup_retry_next(STARTUP_RETRY_NONE, false, true, false, NULL, NULL, 0) ==
          STARTUP_RETRY_D3D12);
    /* names in any case */
    CHECK(startup_retry_next(STARTUP_RETRY_NONE, false, true, false, "D3D12", NULL, 0) ==
          STARTUP_RETRY_LAZY);
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
        s = startup_retry_next(s, cache, true, false, backend, NULL, 0);
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
    CHECK(startup_retry_next(STARTUP_RETRY_NONE, false, true, false, "vulkan", NULL, 0) ==
          STARTUP_RETRY_D3D12);
    CHECK(startup_retry_next(STARTUP_RETRY_NONE, false, true, true, "vulkan", NULL, 0) ==
          STARTUP_RETRY_LAZY);
    CHECK(startup_retry_next(STARTUP_RETRY_NONE, false, false, false, "vulkan", NULL, 0) ==
          STARTUP_RETRY_LAZY);
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
    static const char *const bad[] = {"",
                                      "lazy",
                                      "lazy:",
                                      ":vulkan",
                                      "fast:vulkan",
                                      "lazyx:vulkan",
                                      "laz:vulkan",
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
}

int main(void)
{
    test_table();
    test_chain();
    test_values();
    test_envp();
    test_block();
    return ico_check_summary("startup_retry");
}
