/*
 * port/game/test/poly_flat_test.c
 *
 * IsPointIsInScreen (ico2/omori/src/poly-flat.c, compiled in below so its
 * statics are in reach): the x bound is the 4:3 picture's half-width, the
 * PS2's +-256 pixels, at every native width (v0.6.1, VIS-RULES item 19: its
 * callers are camera decisions, and the camera is the original's; v0.6.0
 * used half the native width, so at w = 688 a point 300 pixels off the
 * centre was in).  The result is the same number as with ScreenWidth / 2.
 * IsPointIsInScreenWide (Ico's own looks, boyact.c) takes half the native
 * width (gsFrame.w): the PS2's window at 4:3, 344 a side at 688, 1280 at
 * 2560.  The projection is a stub that returns a chosen GS point.
 */
#include <stdio.h>
#include <string.h>
#include "GsBase.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include "GifPacket.h"
#include "main.h"
#include "../../../ico2/omori/src/poly-flat.c"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* --- what poly-flat.c needs ---------------------------------------------- */
int ScreenWidth = 512, ScreenHeight = 448;
IcoFrame gsFrame = {512, 448, 8, 512.0f, 4.0f / 3.0f};
char *matrixptr;

static int s_point[4]; /* what sceVu0RotTransPers writes: x, y (1/16 pixel), z, 0 */

void sceVu0UnitMatrix(void *m) {}

void sceVu0MulMatrix(void *d, void *a, void *b) {}

void sceVu0RotTransPers(void *dst, void *m, void *src, int mode)
{
    memcpy(dst, s_point, sizeof(s_point));
}

void CopyMatrix(void *d, void *s) {}

void _InitCurrentMatrix(void) {}

void _SetCurrentMatrix(void *m) {}

int _IsInScreen(volatile int *v)
{
    return 1;
}

void gif_DrawPolyF4(void *a, void *b, void *c, void *d, int r, int g, int bl, int al, int prim) {}

void gif_EndPacket(void) {}

void gif_MakeLine2D(int *a, int *b, long long z0, long long z1, unsigned char *col, int prim) {}

void gif_SetAlpha(long long a, long long m, long long f) {}

void gif_StartPacketPri(int pri) {}

/* a point dx pixels right of the centre (2048), at the vertical centre, depth z */
static float probe(int w, int dx, int z)
{
    int out[4];
    float pos[4] = {0, 0, 0, 1};

    gsFrame.w = w;
    s_point[0] = (0x800 + dx) * 16;
    s_point[1] = 0x800 * 16;
    s_point[2] = z;
    s_point[3] = 0;
    return IsPointIsInScreen(out, pos);
}

/* the same point through IsPointIsInScreenWide */
static float probeWide(int w, int dx, int z)
{
    int out[4];
    float pos[4] = {0, 0, 0, 1};

    gsFrame.w = w;
    s_point[0] = (0x800 + dx) * 16;
    s_point[1] = 0x800 * 16;
    s_point[2] = z;
    s_point[3] = 0;
    return IsPointIsInScreenWide(out, pos);
}

int main(void)
{
    float r;

    /* 4:3: the PS2's window, edges inclusive, just past them out */
    CHECK(probe(512, 0, 0x8000) >= 0.0f);
    CHECK(probe(512, 256, 0x8000) >= 0.0f);
    CHECK(probe(512, -256, 0x8000) >= 0.0f);
    CHECK(probe(512, 257, 0x8000) < 0.0f);
    CHECK(probe(512, -257, 0x8000) < 0.0f);
    CHECK(probe(512, 300, 0x8000) < 0.0f);
    /* the ratio is the same expression as with ScreenWidth / 2 = 256 */
    r = probe(512, 128, 0x8000);
    CHECK(r == (float)((0x800 + 128) * 16 - 0x8000) / (float)((0x800 + 256) * 16 - 0x8000));
    /* wider frames: the same 4:3 window, the same ratio */
    CHECK(probe(688, 256, 0x8000) >= 0.0f);
    CHECK(probe(688, -256, 0x8000) >= 0.0f);
    CHECK(probe(688, 257, 0x8000) < 0.0f);
    CHECK(probe(688, 300, 0x8000) < 0.0f);
    CHECK(probe(688, -345, 0x8000) < 0.0f);
    CHECK(probe(2048, 257, 0x8000) < 0.0f);
    CHECK(probe(2560, 1280, 0x8000) < 0.0f);
    CHECK(probe(2048, 128, 0x8000) == r);
    /* the depth bounds are unchanged by the width */
    CHECK(probe(688, 0, -1) < 0.0f);
    CHECK(probe(2560, 0, 0x0FFFFFF1) < 0.0f);
    /* Ico's looks: the native width, the PS2's window and ratio at 4:3 */
    CHECK(probeWide(512, 256, 0x8000) >= 0.0f);
    CHECK(probeWide(512, 257, 0x8000) < 0.0f);
    CHECK(probeWide(512, 128, 0x8000) == r);
    CHECK(probeWide(688, 300, 0x8000) >= 0.0f);
    CHECK(probeWide(688, 344, 0x8000) >= 0.0f);
    CHECK(probeWide(688, -344, 0x8000) >= 0.0f);
    CHECK(probeWide(688, 345, 0x8000) < 0.0f);
    CHECK(probeWide(2560, 1280, 0x8000) >= 0.0f);
    CHECK(probeWide(2560, 1281, 0x8000) < 0.0f);
    CHECK(probeWide(2560, 0, 0x0FFFFFF1) < 0.0f);
    printf("poly_flat_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
