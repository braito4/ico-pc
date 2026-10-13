/*
 * port/platform/display_clock_none.c
 *
 * No display clock (display_clock.h): every platform but Windows.
 */
#include "display_clock.h"

#include <stdio.h>

bool ico_display_clock_start(void *sdlWindow)
{
    (void)sdlWindow;
    return false;
}

void ico_display_clock_restart(void *sdlWindow)
{
    (void)sdlWindow;
}

int ico_display_clock_next(uint64_t *ns)
{
    (void)ns;
    return -1;
}

const char *ico_display_clock_name(char *buf, unsigned len)
{
    snprintf(buf, len, "none");
    return buf;
}

void ico_display_clock_stop(void) {}

bool ico_display_dwm_refresh(double *hz, uint64_t *periodNs)
{
    (void)hz;
    (void)periodNs;
    return false;
}
