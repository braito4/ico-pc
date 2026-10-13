/* above1x_ref.c: the above-1x resampling on the CPU (above1x_ref.h). */
#include "above1x_ref.h"
#include <math.h>

/* The Bayer matrix by its recursion: M1 = [0], and M2n holds 4 Mn + 0, 2
 * (right), 3 (below), 1 (below right) in its four quadrants. */
uint32_t a1x_bayer8(uint32_t x, uint32_t y)
{
    uint32_t m[8][8];
    m[0][0] = 0;
    for (uint32_t n = 1; n < 8; n *= 2) {
        for (uint32_t r = 0; r < n; r++) {
            for (uint32_t c = 0; c < n; c++) {
                const uint32_t v = 4 * m[r][c];
                m[r][c] = v;
                m[r][c + n] = v + 2;
                m[r + n][c] = v + 3;
                m[r + n][c + n] = v + 1;
            }
        }
    }
    return m[y & 7u][x & 7u];
}

/* the B-spline at distance d16 / 16 texels, times 6 * 16^3:
 * (2 / 3 - d^2 + |d|^3 / 2) within one texel, (2 - |d|)^3 / 6 within two */
static int bsplineAt(int d16)
{
    const int d = d16 < 0 ? -d16 : d16;
    if (d < 16) {
        return 16384 - 96 * d * d + 3 * d * d * d;
    }
    if (d < 32) {
        return (32 - d) * (32 - d) * (32 - d);
    }
    return 0;
}

int a1x_bspline_w(uint32_t f, uint32_t k)
{
    /* tap k is texel i0 - 1 + k, whose centre lies 16 + f - 16 k (in 1/16)
     * before the sample point (texel i0's centre lies f before it) */
    return bsplineAt((int)(f & 15u) + 16 - 16 * (int)k);
}

int a1x_box_w(int lo32, int hi32, int i)
{
    const int a = lo32 > 32 * i ? lo32 : 32 * i;
    const int b = hi32 < 32 * i + 32 ? hi32 : 32 * i + 32;
    return b > a ? b - a : 0;
}

uint32_t a1x_step32(double du, double dx, double ss, double ts)
{
    const double k = fabs(du) / fabs(dx) * ss / ts * 32.0;
    return k >= A1X_STEP_MAX ? A1X_STEP_MAX : (uint32_t)floor(k + 0.5);
}

int a1x_kind(uint32_t step32, int linear)
{
    if (step32 > A1X_BOX_MIN32) {
        return A1X_BOX;
    }
    if (linear && step32 > 0 && step32 < A1X_BSPLINE_MAX32) {
        return A1X_BSPLINE;
    }
    return A1X_GS;
}

static int floorDiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}

#define A1X_TAPS 40

/* an axis's taps: texel indices, weights, their sum; returns the count */
static int axisTaps(int c16, uint32_t step32, int linear, int idx[A1X_TAPS], int w[A1X_TAPS],
                    int *div)
{
    const int kind = a1x_kind(step32, linear);
    if (kind == A1X_BOX) {
        const int s = (int)(step32 < A1X_BOX_MAX32 ? step32 : A1X_BOX_MAX32);
        const int lo = 2 * c16 - s / 2, hi = lo + s;
        int n = 0;
        for (int i = floorDiv(lo, 32); i <= floorDiv(hi - 1, 32) && n < A1X_TAPS; i++) {
            idx[n] = i;
            w[n] = a1x_box_w(lo, hi, i);
            n++;
        }
        *div = s;
        return n;
    }
    const int b = c16 - 8;
    const int i0 = floorDiv(b, 16);
    const uint32_t f = (uint32_t)(b - 16 * i0);
    if (kind == A1X_BSPLINE) {
        for (int k = 0; k < 4; k++) {
            idx[k] = i0 - 1 + k;
            w[k] = a1x_bspline_w(f, (uint32_t)k);
        }
        *div = A1X_BSPLINE_DIV;
        return 4;
    }
    if (linear) { /* the GS's bilinear on this axis */
        idx[0] = i0;
        idx[1] = i0 + 1;
        w[0] = 16 - (int)f;
        w[1] = (int)f;
        *div = 16;
        return 2;
    }
    idx[0] = floorDiv(c16, 16); /* the GS's nearest */
    w[0] = 1;
    *div = 1;
    return 1;
}

void a1x_resample(A1xFetch fetch, void *user, int c16x, int c16y, uint32_t step32x,
                  uint32_t step32y, int linear, uint32_t out[4])
{
    int ix[A1X_TAPS], wx[A1X_TAPS], iy[A1X_TAPS], wy[A1X_TAPS], dx = 1, dy = 1;
    const int nx = axisTaps(c16x, step32x, linear, ix, wx, &dx);
    const int ny = axisTaps(c16y, step32y, linear, iy, wy, &dy);
    uint32_t sum[4] = {0, 0, 0, 0};
    for (int j = 0; j < ny; j++) {
        uint32_t row[4] = {0, 0, 0, 0};
        for (int i = 0; i < nx; i++) {
            uint32_t t[4];
            fetch(user, ix[i], iy[j], t);
            for (int ch = 0; ch < 4; ch++) {
                row[ch] += t[ch] * (uint32_t)wx[i];
            }
        }
        for (int ch = 0; ch < 4; ch++) {
            sum[ch] += ((row[ch] * 16u + (uint32_t)dx / 2u) / (uint32_t)dx) * (uint32_t)wy[j];
        }
    }
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = (sum[ch] + 8u * (uint32_t)dy) / (16u * (uint32_t)dy);
    }
}
