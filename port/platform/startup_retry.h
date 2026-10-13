/*
 * port/platform/startup_retry.h
 *
 * What the game does after the graphics driver crashed while building a
 * pipeline (the crash report's activity note, diag_host.h): the crash
 * handler starts the program again at once, in the same session, with
 * ICO_STARTUP_RETRY=<step>:<backend> in its environment, and the new run
 * (window_host.c) does that step.  Nothing is written to the user folder
 * or the settings: the variable lives only in the relaunched process.
 *
 * The steps, in order; each is taken only when the run before crashed:
 *   cache  the crashing run had loaded the pipeline cache file: it is
 *          deleted and the same backend tries again;
 *   d3d12  a build with the D3D12 backend, whose player did not choose a
 *          backend (backend= / [video] backend): Direct3D 12; when it
 *          cannot start (no device), that run goes on to lazy on Vulkan
 *          itself (window_host.c), as nothing crashed;
 *   lazy   the up-front creation of the pipeline set is skipped: each one
 *          is built when a frame first needs it, on the same backend.
 * After lazy there is nothing left: the crash box is shown.
 *
 * Pure functions: no I/O, no allocation (the crash handler calls them).
 */
#ifndef ICO_PLATFORM_STARTUP_RETRY_H
#define ICO_PLATFORM_STARTUP_RETRY_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#define STARTUP_RETRY_ENV "ICO_STARTUP_RETRY"

typedef enum {
    STARTUP_RETRY_NONE = 0, /* the first run; as a result: no step left */
    STARTUP_RETRY_CACHE,
    STARTUP_RETRY_D3D12,
    STARTUP_RETRY_LAZY,
} StartupRetry;

/* The step after a crash in a run that was at step (NONE: the first run).
 * cacheWasLoaded: the crashing run created its pipeline cache from the
 * file; haveD3d12: the build has the D3D12 backend; backendExplicit: the
 * player named a backend; backendName: the crashing run's backend.
 * reason gets one line in plain words (also for NONE: why nothing is left). */
StartupRetry startup_retry_next(StartupRetry step, bool cacheWasLoaded, bool haveD3d12,
                                bool backendExplicit, const char *backendName, char *reason,
                                size_t reasonLen);

/* "cache", "d3d12", "lazy"; "" for NONE */
const char *startup_retry_name(StartupRetry step);

/* The variable's value for the run after a crash on backendName that takes
 * step next: "<step>:<backend>", the backend being "d3d12" for the d3d12
 * step and backendName otherwise. 0, or -1 when it does not fit or next
 * is NONE. */
int startup_retry_value(StartupRetry next, const char *backendName, char *out, size_t size);

/* Reads a value written by startup_retry_value: 0 with *step and backend,
 * -1 (and *step NONE) for NULL, "" or anything else. */
int startup_retry_parse(const char *value, StartupRetry *step, char *backend, size_t size);

/* What a run at step does, for its first log line ("the pipeline cache
 * file was deleted; Vulkan prepares the shaders again", ...) */
const char *startup_retry_what(StartupRetry step);

/* The relaunch's environment, prepared at start-up so the crash handler
 * neither allocates nor reads a changing environ.
 *
 * POSIX: out[] gets env's pointers without any ICO_STARTUP_RETRY=, then
 * extra (when not NULL), then NULL. Returns the count before the NULL, or
 * -1 when max (out's size, the NULL included) is too small. */
int startup_retry_envp(char *const *env, const char *extra, const char **out, size_t max);

/* Windows: a copy of a CreateProcessW environment block (NAME=value
 * strings, each NUL-terminated, a second NUL at the end) without any
 * ICO_STARTUP_RETRY= (any case), the final NUL left off, so
 * startup_retry_block_finish can add the variable later. Returns the
 * wide characters written, or -1 when cap is too small. */
long startup_retry_block_copy(const wchar_t *block, wchar_t *out, size_t cap);
/* Appends ICO_STARTUP_RETRY=<value> and the block's final NUL after used
 * characters (value ASCII). 0, or -1 when it does not fit. */
int startup_retry_block_finish(wchar_t *out, size_t used, size_t cap, const char *value);

#endif /* ICO_PLATFORM_STARTUP_RETRY_H */
