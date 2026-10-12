/*
 * port/platform/test/movie_pad_test.c
 *
 * The pad as a movie sees it.  The attract movie's skip is main.c's
 * movie_abort_check: once a vsync, ExecKeyInput() and then
 * (pad[0].flags & 0x800), from inside the movie player's busy loop
 * (port/fmv/movie.c next_vsync: RotateThreadReadyQueue, then
 * ico_sched_spin_vsync).  This runs that path with the game's own code:
 *
 *   sugipon/src/keyInput.c   InitKeyInput, ExecKeyInput
 *   fumi/ios/pad.c           iosPadDevInit, iosPadDevRead, the pad manager
 *                            thread (priority 17), iosPadRead
 *   fumi/ios/shockdriver.c   the vibration driver pad.c calls every read
 *   fumi/ios/thread.c, message.c, memory.c   the thread and message layer
 *   port/input               libpad (pad_host.c), the binding layer
 *
 * on the fiber scheduler, with a thread (priority 27) that does what
 * the movie loop does: spin to the next vsync, poll.  The host side feeds
 * the input layer as input_sdl.c does each vsync (ico_bindings_step on a
 * snapshot of the devices, ico_input_set_vpad): a gamepad's Start button,
 * then the key bound to Start, then a one vsync tap of each.
 *
 * Asserted: the START edge (pad[0].flags & 0x800) reaches the poll within a
 * vsync of the press for every source, exactly once per press, with the
 * button held the pad's word (pad[0].now) shows it, and
 * ico_input_start_source names the source for the log line
 * "fmv: skip (START, <source>)" (port/fmv/movie.c).
 */
#include <stdio.h>
#include <string.h>

#include <eekernel.h>
#include <eeregs.h>
#include <libvu0.h>

#include "input.h"
#include "ios.h"
#include "keyInput.h"
#include "main.h"
#include "memory.h"
#include "message.h"
#include "pad.h"
#include "thread.h"

#include "../arena.h"
#include "../kernel_host.h"
#include "../sched.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* --- what the rest of the game would define ---------------------------------- */

void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("FAIL debug_assertMessage %s:%d: %s\n", file, line, mes);
    fails++;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    fails++;
}

int odd_even;
int frame_count;
char *matrixptr;
int debug_hand_camera;
int systemStatus[12];
PadState pad[16];
int IosPadLock;
IosMemPart *ios_partition_root;
IosMemPart *ios_partition_event;
const PadActDef shockList[1] = {{0}};

/* the camera and stick arithmetic pad.c links against (port/math and the game
   in the program): not read by the Start poll, so inert here */
float FSqrt(float x);
void sceVu0ApplyMatrix(void *dst, void *m, void *v);
void sceVu0Normalize(void *dst, void *src);
void sceVu0TransposeMatrix(void *dst, void *src);

float FSqrt(float x)
{
    float r = x > 1.0f ? x : 1.0f;
    int i;

    for (i = 0; x > 0.0f && i < 16; i++) {
        r = 0.5f * (r + x / r);
    }
    return x > 0.0f ? r : 0.0f;
}

void sceVu0ApplyMatrix(void *dst, void *m, void *v)
{
    (void)m;
    memcpy(dst, v, 16);
}

void sceVu0Normalize(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void sceVu0TransposeMatrix(void *dst, void *src)
{
    memcpy(dst, src, 64);
}

float _GetDirection(float *dir);
void _PushVu0Registers(void);
void _PopVu0Registers(void);

float _GetDirection(float *dir)
{
    (void)dir;
    return 0.0f;
}

void _PushVu0Registers(void) {}

void _PopVu0Registers(void) {}

/* --- the miniature game ------------------------------------------------------ */

#define TICKS 120
#define START 0x800

static IOSThread idleTh;
static IOSThread schedTh;
static IOSThread mainTh;
static char idleStack[0x1800] __attribute__((aligned(16)));
static char schedStack[0x1800] __attribute__((aligned(16)));
static char mainStack[0x1800] __attribute__((aligned(16)));
static IosMsgQueue schedQ;
static IosMsgWord schedBuf[8];

/* per vsync: what Main's poll read */
static int g_edge[TICKS];
static int g_now[TICKS];
static int g_done;
static unsigned int g_lastVsync = ~0u;

static void idle(void)
{
    iosThreadSetPri(0, 0x20);
    for (;;) {
        ico_sched_spin_vsync();
    }
}

/* the vsync message sink main.c's scheduler() would be */
static void scheduler(void)
{
    IosMsgWord msg[4];
    iosMsgQueueCreate(&schedQ, schedBuf, 8);
    iosMsgSetEvent(2, &schedQ, 2);
    for (;;) {
        iosMsgRecv(&schedQ, msg, 1);
    }
}

/* main.c movie_abort_check, as the game has it */
static int abort_check(void)
{
    int ret = 0;
    if (g_lastVsync != ico_sched_vsync_count()) {
        g_lastVsync = ico_sched_vsync_count();
        ExecKeyInput();
        ret = 0;
        ret = (pad[0].flags & START) != ret;
    }
    return ret;
}

static void mainThread(void)
{
    struct SemaParam sp = {0, 1, 0, 0, 0, 0};
    int tick;
    IosPadLock = CreateSema(&sp);
    systemStatus[0] = 0;
    systemStatus[1] = 1;
    InitKeyInput(0);
    for (tick = 0; tick < TICKS; tick++) {
        /* movie.c next_vsync */
        RotateThreadReadyQueue(27);
        ico_sched_spin_vsync();
        g_edge[tick] = abort_check();
        g_now[tick] = pad[0].now;
    }
    g_done = 1;
    for (;;) {
        iosThreadSleep();
    }
}

static void boot(void *arg)
{
    (void)arg;
    ChangeThreadPriority(GetThreadId(), 14);
    iosThreadInit();
    ios_partition_root =
        iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    ios_partition_event = iosMallocSetPartition(ios_partition_root, 262144, 16);
    iosMsgInit();
    iosThreadCreate(&idleTh, 1, idle, 0, idleStack, sizeof idleStack, 0x1B);
    iosThreadStart(&idleTh);
    iosThreadCreate(&schedTh, 1, scheduler, 0, schedStack, sizeof schedStack, 0xF);
    iosThreadStart(&schedTh);
    iosThreadCreate(&mainTh, 3, mainThread, 0, mainStack, sizeof mainStack, 27);
    iosThreadStart(&mainTh);
    iosThreadSleep();
}

/* --- the host side ----------------------------------------------------------- */

static IcoBindings s_bind;

/* input_sdl.c's ico_input_sdl_update for one snapshot of the devices */
static void feed(const IcoInputRaw *raw)
{
    IcoVirtualPad v;

    ico_bindings_step(&s_bind, raw, &v);
    ico_input_set_start_sources(ico_bindings_start_sources(&s_bind, raw));
    ico_input_set_vpad(&v);
}

static void vsync(int field)
{
    ico_sched_vsync_advance();
    if (field) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
    ico_kernel_raise_intc(2);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
}

/* one source's presses: hold [from, to) */
typedef struct Press {
    int from, to;
    int gamepad; /* else the keyboard */
    const char *name;
} Press;

int main(void)
{
    static const Press presses[] = {
        {20, 26, 1, "gamepad"},  /* held for six vsyncs */
        {40, 46, 0, "keyboard"}, /* the key bound to Start */
        {60, 61, 1, "gamepad"},  /* a one vsync tap */
        {80, 81, 0, "keyboard"},
    };
    static char nameAt[TICKS][16];
    IcoInputRaw raw;
    int i, t, edges, first;

    CHECK(ico_arena_init() == 0);
    ico_sched_reset();
    ico_kernel_reset();
    ico_bindings_defaults(&s_bind);
    s_bind.gamepad = 1;
    s_bind.keyboard = 1;
    ico_input_set_live(1);
    ico_sched_boot(boot, NULL, 1);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    for (t = 0; t < TICKS; t++) {
        memset(&raw, 0, sizeof(raw));
        raw.gamepads = 1;
        raw.dt = 1.0f / 60.0f;
        for (i = 0; i < (int)(sizeof(presses) / sizeof(presses[0])); i++) {
            if (t >= presses[i].from && t < presses[i].to) {
                if (presses[i].gamepad) {
                    raw.gp[ICO_GP_START] = 1.0f;
                } else {
                    raw.key[ICO_KEY_RETURN] = 1;
                }
            }
        }
        feed(&raw);
        vsync(t & 1);
        strncpy(nameAt[t], ico_input_start_source(), sizeof(nameAt[t]) - 1);
    }
    CHECK(g_done);

    /* nothing before the first press: the pad came up and reads released */
    for (t = 0; t < presses[0].from; t++) {
        CHECK(!g_edge[t] && !(g_now[t] & START));
    }
    for (i = 0; i < (int)(sizeof(presses) / sizeof(presses[0])); i++) {
        const Press *p = &presses[i];

        edges = 0;
        first = -1;
        for (t = p->from; t < p->to + 4; t++) {
            if (g_edge[t]) {
                edges++;
                if (first < 0) {
                    first = t;
                }
            }
        }
        printf("%s press at vsync %d: START edge %d time(s), first at vsync %d, word 0x%x\n",
               p->name, p->from, edges, first, first >= 0 ? g_now[first] : 0);
        /* exactly one edge, within a vsync of the press */
        CHECK(edges == 1);
        CHECK(first >= p->from && first <= p->from + 1);
        /* the word shows the button while it is held */
        if (p->to - p->from > 2) {
            CHECK((g_now[p->from + 2] & START) != 0);
        }
        /* and the log line names the source */
        CHECK(strcmp(nameAt[p->from], p->name) == 0);
        /* released again */
        CHECK(!(g_now[p->to + 3] & START));
    }
    CHECK(strcmp(nameAt[5], "none") == 0);
    ico_sched_reset();

    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
