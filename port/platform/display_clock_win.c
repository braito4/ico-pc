/*
 * port/platform/display_clock_win.c
 *
 * The display clock on Windows (display_clock.h): a thread waits on each
 * vblank of the DXGI output whose monitor is the window's
 * (MonitorFromWindow; DXGI_OUTPUT_DESC.Monitor) with
 * IDXGIOutput::WaitForVBlank and notes SDL_GetTicksNS when the wait
 * returned.  dxgi.dll is the system directory's: an effects program's
 * dxgi.dll beside the executable (ReShade, rhi/d3d12/d3d12_device.c) is
 * not loaded for this.  The thread runs at time-critical priority (a note
 * is a wake-up's time, so a late wake-up is a late note) and holds its
 * lock only to note a time.  It stops itself when a wait fails or when
 * DC_FAST_MAX waits in a row return within DC_FAST_NS of the one before
 * (no vblank to wait for: a display switched off, a remote session).
 *
 * Stopping never waits for the thread: it is asked to end, and when it is
 * still in a wait it is left to end on its own after it (one refresh), its
 * state with it, as vk_swapchain.c's present-wait thread is at shutdown.
 */
#ifndef COBJMACROS
#define COBJMACROS
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "display_clock.h"

#define DC_FAST_NS 1000000ull
#define DC_FAST_MAX 16

/* IID_IDXGIFactory1 (dxgi.h), here so the program needs no libdxguid */
static const GUID kIidFactory1 = {
    0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};

typedef HRESULT(WINAPI *PFN_DcCreateFactory1)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_DcCompositionTiming)(HWND, DWM_TIMING_INFO *);

typedef struct DcClock {
    SDL_Mutex *lock;
    SDL_Thread *thread;
    IDXGIOutput *output; /* the thread's: released by it when it ends */
    bool quit, failed, exited, abandoned;
    uint64_t note[ICO_DISPLAY_CLOCK_NOTES];
    unsigned head, count;
    char name[64];
} DcClock;

static DcClock *s_dc;   /* NULL: no clock */
static char s_name[64]; /* the last output's name, kept for the log after a stop */
static HMODULE s_dxgi;  /* the system's dxgi.dll, kept loaded */
static HMODULE s_dwm;   /* the system's dwmapi.dll, kept loaded */

/* name from the system directory (System32), not the program's */
static HMODULE dc_load_system(const char *name)
{
    char path[MAX_PATH];
    const UINT n = GetSystemDirectoryA(path, (UINT)sizeof(path));

    if (n == 0 || n + 1 + strlen(name) + 1 > sizeof(path)) {
        return NULL;
    }
    snprintf(path + n, sizeof(path) - n, "\\%s", name);
    return LoadLibraryA(path);
}

static void dc_free(DcClock *c)
{
    if (c->lock) {
        SDL_DestroyMutex(c->lock);
    }
    free(c);
}

static int SDLCALL dc_thread(void *u)
{
    DcClock *c = u;
    uint64_t prev = SDL_GetTicksNS();
    unsigned fast = 0;
    bool stop = false;

    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL);
    while (!stop) {
        const HRESULT hr = IDXGIOutput_WaitForVBlank(c->output);
        const uint64_t t = SDL_GetTicksNS();

        SDL_LockMutex(c->lock);
        if (FAILED(hr)) {
            c->failed = true;
        } else if (t - prev < DC_FAST_NS) {
            /* not a new vblank: not noted */
            if (++fast >= DC_FAST_MAX) {
                c->failed = true;
            }
        } else {
            fast = 0;
            if (c->count == ICO_DISPLAY_CLOCK_NOTES) {
                c->head = (c->head + 1) % ICO_DISPLAY_CLOCK_NOTES; /* the oldest, never taken */
                c->count--;
            }
            c->note[(c->head + c->count++) % ICO_DISPLAY_CLOCK_NOTES] = t;
        }
        prev = t;
        stop = c->quit || c->failed;
        SDL_UnlockMutex(c->lock);
    }
    IDXGIOutput_Release(c->output);
    c->output = NULL;
    SDL_LockMutex(c->lock);
    c->exited = true;
    const bool own = c->abandoned; /* ico_display_clock_stop did not wait for it */
    SDL_UnlockMutex(c->lock);
    if (own) {
        dc_free(c);
    }
    return 0;
}

/* The output whose monitor is mon (a reference), and its name */
static IDXGIOutput *dc_find_output(HMONITOR mon, char *name, size_t nameLen)
{
    if (!s_dxgi) {
        s_dxgi = dc_load_system("dxgi.dll");
    }
    if (!s_dxgi) {
        fprintf(stderr, "window: display clock: no dxgi.dll in the system directory\n");
        return NULL;
    }
    const PFN_DcCreateFactory1 create =
        (PFN_DcCreateFactory1)(void *)GetProcAddress(s_dxgi, "CreateDXGIFactory1");
    IDXGIFactory1 *f = NULL;

    if (!create || FAILED(create(&kIidFactory1, (void **)&f)) || !f) {
        fprintf(stderr, "window: display clock: no DXGI factory\n");
        return NULL;
    }
    IDXGIOutput *found = NULL;

    for (UINT a = 0; !found; a++) {
        IDXGIAdapter1 *ad = NULL;

        if (FAILED(IDXGIFactory1_EnumAdapters1(f, a, &ad)) || !ad) {
            break; /* DXGI_ERROR_NOT_FOUND: no more */
        }
        for (UINT o = 0; !found; o++) {
            IDXGIOutput *out = NULL;
            DXGI_OUTPUT_DESC d;

            if (FAILED(IDXGIAdapter1_EnumOutputs(ad, o, &out)) || !out) {
                break;
            }
            memset(&d, 0, sizeof(d));
            if (SUCCEEDED(IDXGIOutput_GetDesc(out, &d)) && d.Monitor == mon) {
                found = out;
                if (WideCharToMultiByte(CP_UTF8, 0, d.DeviceName, -1, name, (int)nameLen, NULL,
                                        NULL) == 0) {
                    snprintf(name, nameLen, "adapter %u output %u", (unsigned)a, (unsigned)o);
                }
            } else {
                IDXGIOutput_Release(out);
            }
        }
        IDXGIAdapter1_Release(ad);
    }
    IDXGIFactory1_Release(f);
    return found;
}

bool ico_display_clock_start(void *sdlWindow)
{
    if (s_dc) {
        return true;
    }
    if (!sdlWindow) {
        return false;
    }
    const HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties((SDL_Window *)sdlWindow),
                                                   SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    const HMONITOR mon = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : NULL;

    if (!mon) {
        fprintf(stderr, "window: display clock: the window has no monitor\n");
        return false;
    }
    DcClock *c = calloc(1, sizeof(*c));

    if (!c) {
        return false;
    }
    c->output = dc_find_output(mon, c->name, sizeof(c->name));
    if (!c->output) {
        fprintf(stderr, "window: display clock: no output for the window's monitor\n");
        free(c);
        return false;
    }
    c->lock = SDL_CreateMutex();
    if (c->lock) {
        c->thread = SDL_CreateThread(dc_thread, "display-clock", c);
    }
    if (!c->thread) {
        fprintf(stderr, "window: display clock: no thread (%s)\n", SDL_GetError());
        IDXGIOutput_Release(c->output);
        dc_free(c);
        return false;
    }
    snprintf(s_name, sizeof(s_name), "%s", c->name);
    s_dc = c;
    return true;
}

void ico_display_clock_stop(void)
{
    DcClock *c = s_dc;

    s_dc = NULL;
    if (!c) {
        return;
    }
    SDL_Thread *const thread = c->thread; /* c is the thread's once abandoned */

    SDL_LockMutex(c->lock);
    c->quit = true;
    const bool exited = c->exited;

    if (!exited) {
        c->abandoned = true; /* it frees itself after its wait */
    }
    SDL_UnlockMutex(c->lock);
    if (exited) {
        SDL_WaitThread(thread, NULL); /* it has only to return */
        dc_free(c);
    } else {
        SDL_DetachThread(thread);
    }
}

void ico_display_clock_restart(void *sdlWindow)
{
    ico_display_clock_stop();
    (void)ico_display_clock_start(sdlWindow);
}

int ico_display_clock_next(uint64_t *ns)
{
    DcClock *c = s_dc;
    int r;

    if (!c) {
        return -1;
    }
    SDL_LockMutex(c->lock);
    if (c->count) {
        *ns = c->note[c->head];
        c->head = (c->head + 1) % ICO_DISPLAY_CLOCK_NOTES;
        c->count--;
        r = 1;
    } else {
        r = c->failed ? -1 : 0;
    }
    SDL_UnlockMutex(c->lock);
    return r;
}

const char *ico_display_clock_name(char *buf, unsigned len)
{
    snprintf(buf, len, "%s", s_name[0] ? s_name : "none");
    return buf;
}

bool ico_display_dwm_refresh(double *hz, uint64_t *periodNs)
{
    if (!s_dwm) {
        s_dwm = dc_load_system("dwmapi.dll");
    }
    if (!s_dwm) {
        return false;
    }
    const PFN_DcCompositionTiming timing =
        (PFN_DcCompositionTiming)(void *)GetProcAddress(s_dwm, "DwmGetCompositionTimingInfo");
    DWM_TIMING_INFO info;
    LARGE_INTEGER freq;

    if (!timing) {
        return false;
    }
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    /* NULL: the compositor's own timing (Windows 8.1 and later take no
       window) */
    if (FAILED(timing(NULL, &info)) || info.rateRefresh.uiDenominator == 0 ||
        !QueryPerformanceFrequency(&freq) || freq.QuadPart <= 0) {
        return false;
    }
    *hz = (double)info.rateRefresh.uiNumerator / (double)info.rateRefresh.uiDenominator;
    *periodNs = (uint64_t)((double)info.qpcRefreshPeriod * 1e9 / (double)freq.QuadPart + 0.5);
    return *hz > 0.0;
}
