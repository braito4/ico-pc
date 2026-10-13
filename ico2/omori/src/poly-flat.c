#include "Matrix.h"
#include "matrixDrive.h"
#include "GsBase.h"
#include "main.h"

/* the world-space matrix before_DrawLine copies the caller's into and every
   line transform reads back */
float drawline_ws_matrix[16] = {0};

#include "poly-flat.h"
#include <libvu0.h>
#include "GifPacket.h"

static inline unsigned char DrawLineTrans(int *dst, void *src) /* derived name */
{
    sceVu0RotTransPers(dst, drawline_ws_matrix, src, 1);
    return _IsInScreen(dst);
}

static inline void DrawLineOffset(int *p) /* derived name */
{
    p[0] -= 0x8000;
    p[1] -= 0x8000;
}

void before_DrawPolygon(void)
{
    gif_StartPacketPri(11);
    gif_SetAlpha(1, 2, 0x40);
}

void after_DrawPolygon(void)
{
    gif_EndPacket();
}

inline void DrawPolygon(void *a, void *b, void *c, void *d, unsigned char *col, void *mtx)
{
    _InitCurrentMatrix();
    _SetCurrentMatrix(mtx);
    gif_DrawPolyF4(a, b, c, d, col[0], col[1], col[2], col[3], 1);
}

/* PC port: the screen test at a half-width hw (the PS2's was ScreenWidth
   / 2, 256), for _IsInScreen2 and its wide form below */
static float isInScreen2At(int *p, int hw) /* port name */
{
    int hh;
    float rx;
    float ry;

    if (p[2] < 0) {
        return -1.0f;
    }
    if (0x0FFFFFF0 < p[2]) {
        return -1.0f;
    }
    if (p[0] < (0x800 - hw) * 16) {
        return -1.0f;
    }
    if ((0x800 + hw) * 16 < p[0]) {
        return -1.0f;
    }
    hh = ScreenHeight / 2;
    if (p[1] < (0x800 - hh) * 16) {
        return -1.0f;
    }
    if ((0x800 + hh) * 16 < p[1]) {
        return -1.0f;
    }

    rx = (float)(p[0] - 0x8000) / (float)((0x800 + hw) * 16 - 0x8000);
    if (rx < 0.0f) {
        rx = -rx;
    }
    ry = (float)(p[2] - 0x8000) / (float)((0x800 + hh) * 16 - 0x8000);
    if (ry < 0.0f) {
        ry = -ry;
    }

    if (ry < rx) {
        ry = rx;
    }

    return ry;
}

/* PC port: two widths for two kinds of caller.
   The camera's decisions take the 4:3 picture's half-width at every frame
   width, the PS2's ScreenWidth / 2 (_IsInScreen2, IsPointIsInScreen):
   camera-root.c Camctrl_Exec (a blend inserted when the camera's target
   leaves the screen) and act-game.c ACTGame_InsertCamera_GirlIsPinch (a
   camera inserted when the girl is off the screen).  With the native width
   the camera of a wide picture would behave differently from the
   original's, reaching angles the PS2 never framed; the camera is the
   original's, the wider picture only shows more of what it frames.  Ico's
   own looks take the native frame's half-width gsFrame.w / 2
   (IsPointIsInScreenWide; 256 at 4:3, so the PS2's there): boyact.c's
   private-camera gate (boyPrivInsCamInScreen), the look at the girl when
   he calls her and the look at a weapon, so he looks at what the player
   sees at the sides of a wide picture.  The bird's own test (act_bird.c)
   keeps the frame's width too. */
static float _IsInScreen2(int *p)
{
    return isInScreen2At(p, ICO_FRAME_BASE_W / 2);
}

inline float IsPointIsInScreen(void *out, void *pos)
{
    float buf[16];
    sceVu0UnitMatrix(buf);
    sceVu0MulMatrix(buf, matrixptr + 0x80, buf);
    sceVu0MulMatrix(buf, matrixptr + 0xC0, buf);
    sceVu0RotTransPers(out, buf, pos, 1);
    return _IsInScreen2(out);
}

/* PC port: IsPointIsInScreen at the native frame's width (above) */
float IsPointIsInScreenWide(void *out, void *pos)
{
    float buf[16];
    sceVu0UnitMatrix(buf);
    sceVu0MulMatrix(buf, matrixptr + 0x80, buf);
    sceVu0MulMatrix(buf, matrixptr + 0xC0, buf);
    sceVu0RotTransPers(out, buf, pos, 1);
    return isInScreen2At(out, gsFrame.w / 2); /* FRAME */
}

void before_DrawLine(void *m)
{
    CopyMatrix(drawline_ws_matrix, m);
    gif_StartPacketPri(11);
}

void after_DrawLine(void)
{
    gif_EndPacket();
}

inline void do_DrawLine(void *from, void *to, unsigned int *c, int unused)
{
    unsigned char col[4] = {c[0], c[1], c[2], c[3]};
    int v0[4];
    int v1[4];

    if (DrawLineTrans(v0, from) == 0)
        return;
    if (DrawLineTrans(v1, to) == 0)
        return;

    DrawLineOffset(v0);
    DrawLineOffset(v1);

    gif_MakeLine2D(v0, v1, v0[2], v1[2], col, 1);
}
