/*
 * port/platform/display_clock.h
 *
 * The display's vblanks on the window's clock (SDL_GetTicksNS), for the
 * vsync lock (window_host.c).  Windows (display_clock_win.c): a thread
 * waits on each vblank of the output the window is on
 * (IDXGIOutput::WaitForVBlank, from the system's own dxgi.dll) and notes
 * when the wait returned; every other platform (display_clock_none.c): no
 * clock.  The window drains the notes every vsync; nothing waits on the
 * thread: it holds its lock only to note a time, and stopping it never
 * blocks (a thread still in a wait ends on its own after it).
 */
#ifndef ICO_PLATFORM_DISPLAY_CLOCK_H
#define ICO_PLATFORM_DISPLAY_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

/* The notes the clock keeps (the oldest dropped when nobody takes them). */
#define ICO_DISPLAY_CLOCK_NOTES 256

/* Starts the clock on the output sdlWindow (an SDL_Window *) is on; true
 * when it runs (already running: true, unchanged).  false: no clock (not
 * Windows, no output found, no thread). */
bool ico_display_clock_start(void *sdlWindow);

/* Stops the clock and starts it again on the output the window is on now
 * (the window moved to another display). */
void ico_display_clock_restart(void *sdlWindow);

/* The next vblank noted, oldest first: 1 one taken (*ns), 0 none yet, -1
 * the clock is not running (never started, stopped, or it stopped itself:
 * a wait failed, or 16 waits in a row returned within 1 ms of the one
 * before). */
int ico_display_clock_next(uint64_t *ns);

/* The output's name for the log ("\\.\DISPLAY1"; "none" without a clock). */
const char *ico_display_clock_name(char *buf, unsigned len);

/* Stops the clock (nothing when it is not running). */
void ico_display_clock_stop(void);

/* The compositor's refresh, once (Windows: DwmGetCompositionTimingInfo,
 * the primary display's): its rate in Hz and its period in ns.  false
 * elsewhere, or when it gives none. */
bool ico_display_dwm_refresh(double *hz, uint64_t *periodNs);

#endif /* ICO_PLATFORM_DISPLAY_CLOCK_H */
