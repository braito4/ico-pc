/*
 * port/include/ico_frame.h
 *
 * The native frame: the GS window the game builds a tick for, as wide as
 * the display aspect asks with the PS2's height and vertical field of view.
 * The projection never changes with the aspect (a world point lands on the
 * same GS X at every width, about the centre 2048); only the window's
 * extent does: w GS pixels, 512 at 4:3 exactly.
 *
 * port/game/video_options.c builds one from the options in force
 * (ico_video_frame); ico2/seki/src/GsBase.c latches it at each flip as
 * gsFrame, which every frame extent of the next tick reads.
 * port/shaders/shader_consts.h mirrors the constants for the shaders' side
 * (rd_present_test asserts they agree).
 *
 * Plain C with no dependencies, included by a relative path
 * ("../include/ico_frame.h" from port, "../../../port/include/ico_frame.h"
 * from ico2/seki/include) so every target gets it without an include
 * directory.
 */
#ifndef ICO_FRAME_H
#define ICO_FRAME_H

/* The PS2's 4:3 width: the projection's scale, the 2D layer's 640 grid and
   every GS buffer address are sized by it, whatever the frame's width. */
#define ICO_FRAME_BASE_W 512
/* The widest frame, as a multiple of the 4:3 width (60:9), and its width. */
#define ICO_FRAME_K_MAX 5
#define ICO_FRAME_W_MAX 2560
/* The widest display aspect, 4/3 times ICO_FRAME_K_MAX. */
#define ICO_FRAME_ASPECT_MAX (4.0f / 3.0f * ICO_FRAME_K_MAX)

typedef struct IcoFrame {
    int w;        /* the native GS width: 16 * ceil(32 k), 512 at 4:3 exactly */
    int h;        /* ScreenHeight (448 NTSC, 512 PAL) */
    int fbw;      /* (w + 63) / 64: the FRAME / TEX0 FBW of a frame-sized buffer */
    float wExact; /* 512 k: the visible width; the presenter crops (w - wExact) / 2 a side */
    float aspect; /* the display aspect the frame is built for, 4/3 exactly when w is 512 */
} IcoFrame;

/* The native width for a display aspect: with k = aspect / (4/3), clamped
   to [1, ICO_FRAME_K_MAX], 16 * ceil(32 k), a multiple of 16 so the frame's
   halves, quarters and eighths start their 4:3 part on a whole pixel.  512
   when !(k > 1 + 1e-5) (a NaN aspect too).  32 k is taken 1e-4 short of the
   next whole number so the float error of a listed aspect (48:9 is 128.0000038)
   does not round up a step; past the 1e-5 edge 32 k is 32.0003 or more. */
static inline int ico_frame_width_for(float aspect)
{
    double k = (double)aspect * 0.75;
    double x;
    int n;

    if (!(k > 1.0 + 1e-5)) {
        return ICO_FRAME_BASE_W;
    }
    if (k > (double)ICO_FRAME_K_MAX) {
        k = (double)ICO_FRAME_K_MAX;
    }
    x = 32.0 * k - 1e-4;
    n = (int)x;
    if ((double)n < x) {
        n++;
    }
    return 16 * n;
}

/* A frame for a display aspect and a height. */
static inline void ico_frame_make(IcoFrame *f, float aspect, int h)
{
    f->w = ico_frame_width_for(aspect);
    f->h = h;
    f->fbw = (f->w + 63) / 64;
    if (f->w == ICO_FRAME_BASE_W) {
        f->wExact = (float)ICO_FRAME_BASE_W;
        f->aspect = 4.0f / 3.0f;
    } else {
        const double k = (double)aspect * 0.75;
        const double km = (double)ICO_FRAME_K_MAX;
        const double we = (double)ICO_FRAME_BASE_W * (k < km ? k : km);

        f->wExact = we < (double)f->w ? (float)we : (float)f->w;
        f->aspect = aspect < ICO_FRAME_ASPECT_MAX ? aspect : ICO_FRAME_ASPECT_MAX;
    }
}

/* A length v of the 4:3 frame at the frame's width: v * w / 512 in
   integers (towards zero), v itself at w == 512. */
static inline int ico_frame_x(const IcoFrame *f, int v)
{
    if (f->w == ICO_FRAME_BASE_W) {
        return v;
    }
    return (int)((long long)v * f->w / ICO_FRAME_BASE_W);
}

/* TEX0's TW / TH for a buffer of n texels: ceil(log2 n), 0 for n <= 1. */
static inline int ico_frame_tw(int texels)
{
    int n = 0;

    while (n < 30 && (1 << n) < texels) {
        n++;
    }
    return n;
}

#endif /* ICO_FRAME_H */
