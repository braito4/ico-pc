/* vu1_ref.c: VU1 state and the upload routines (the SET_* entries), shared
 * by the program references. Line numbers are ico2/vusrc files. */
#include "vu1_ref_internal.h"

void vu1ref_init(Vu1Ref *r)
{
    memset(r, 0, sizeof(*r));
    r->vf[0][3] = 1.0f;
}

void vu1ref_load_common(Vu1Ref *r, const float qw[16][4])
{
    memcpy(r->mem[0], qw, 16 * sizeof(Vec4));
}

void vu1ref_set_uv_offset(Vu1Ref *r, const float qw[4])
{
    /* vu1_common.h:57-62: lq vf30, 2; move.xy vf30, vf31; sq vf30, 2 */
    r->mem[2][0] = qw[0];
    r->mem[2][1] = qw[1];
}

void vu1ref_normal_set_matrix(Vu1Ref *r, const float qw[12][4])
{
    /* normal_c.vsm:54-81 (normal_l.vsm:59-86 is the same) */
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
    memcpy(r->mem[16], qw, 12 * sizeof(Vec4));
    v4_copy(r->mem[37], r->mem[3]);
    v4_copy(r->vf[31], r->mem[3]);
}

void vu1ref_normal_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* normal_c.vsm:88-105 */
    memcpy(r->mem[28], qw, 8 * sizeof(Vec4));
    v4_copy(r->vf[31], qw[7]);
}

void vu1ref_cluster_set_matrix(Vu1Ref *r, const float (*qw)[4])
{
    /* cluster.vsm:69-87: vi06 = qw[0].x; mem[2].w = qw[0].w; then a
     * do-while copies vi06 quadwords from qw[1] to mem[16..] (decrement,
     * load, branch while non-zero, store in the delay slot) */
    int32_t n = vu_int(qw[0][0]);
    if (n <= 0) {
        n = 1; /* the VU would wrap a 16-bit count of 0; the game never sends one */
    }
    r->vf[30][0] = r->mem[2][0];
    r->vf[30][1] = r->mem[2][1];
    r->vf[30][2] = r->mem[2][2];
    r->vf[30][3] = qw[0][3];
    r->mem[2][3] = qw[0][3];
    int dst = 16;
    int src = 1;
    do {
        n -= 1;
        v4_copy(r->vf[28], qw[src++]);
        v4_copy(r->mem[dst++ & (VU1_MEM_QW - 1)], r->vf[28]);
    } while (n != 0);
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[9 + i], r->mem[4 + i]);
    }
    r->vi[6] = 0;
}

void vu1ref_cluster_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* cluster.vsm:98-107 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[13 + i], qw[i]);
    }
}

void vu1ref_mesh_set_matrix(Vu1Ref *r, const float qw[4][4])
{
    /* mesh.vsm:85-94: vf14.w = 16777214, vf13 = 0, vf14.xy = 4094 (z kept) */
    r->vf[14][3] = 16777214.0f;
    memset(r->vf[13], 0, sizeof(Vec4));
    r->vf[14][0] = 4094.0f;
    r->vf[14][1] = 4094.0f;
    for (int i = 0; i < 4; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

void vu1ref_mesh_set_light(Vu1Ref *r, const float qw[8][4])
{
    /* mesh.vsm:102-111 */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[5 + i], qw[i]);
    }
}

void vu1ref_particle_set_matrix(Vu1Ref *r, const float qw[8][4])
{
    /* particle.vsm:43-52, then falls into BEGIN_PARTICLE (E bit) */
    for (int i = 0; i < 8; i++) {
        v4_copy(r->vf[1 + i], qw[i]);
    }
}

int vu1ref_kicks(const VuBatchOut *out, int *kicks)
{
    int n = 0;
    for (int k = 2; k < out->count; k++) {
        if (!vu_gs_adc(&out->v[k])) {
            kicks[n++] = k;
        }
    }
    return n;
}

int vu1ref_static_kicks(const float *stw, const int *batchFirst, int count, int *kicks)
{
    int n = 0;
    for (int k = 0; k < count; k++) {
        if (k - batchFirst[k] < 2) {
            continue;
        }
        if (stw && (stw[k] < 1.0f || stw[k - 1] < 1.0f)) {
            continue;
        }
        kicks[n++] = k;
    }
    return n;
}

int vu1ref_region_draw(int preset, int freeCamera, int insideA, int insideB, int insideC, int hide,
                       int reaches)
{
    (void)preset; /* one camera: the same rule in both presets */
    /* vu_content_hidden */
    if (hide) {
        return VU1REF_DRAW_NONE;
    }
    if (insideA && insideB && insideC) {
        return VU1REF_DRAW_EXACT;
    }
    /* vu_triangle_out: the free camera, then vu_cut_reaches_picture */
    if (freeCamera) {
        return VU1REF_DRAW_CLIPPED;
    }
    return reaches ? VU1REF_DRAW_NONE : VU1REF_DRAW_CLIPPED;
}

int vu1ref_particle_draw(int preset, int freeCamera, int drawn, int shown, int reaches)
{
    (void)preset;
    if (!shown) {
        return VU1REF_DRAW_NONE;
    }
    if (drawn) {
        return VU1REF_DRAW_EXACT;
    }
    if (freeCamera) {
        return VU1REF_DRAW_CLIPPED;
    }
    return reaches ? VU1REF_DRAW_NONE : VU1REF_DRAW_CLIPPED;
}

/* vu_clip_corners in two dimensions: poly[0..n) cut to s * (p[axis] - at)
 * >= 0; returns the corner count (at most n + 1) */
static int cutPolygon(float (*poly)[2], int n, int axis, float at, float sgn)
{
    float q[VU1REF_POLY_MAX][2];
    int m = 0;
    for (int i = 0; i < n; i++) {
        const float *a = poly[i], *b = poly[i + 1 < n ? i + 1 : 0];
        const float da = sgn * (a[axis] - at), db = sgn * (b[axis] - at);
        if (da >= 0.0f && m < VU1REF_POLY_MAX) {
            q[m][0] = a[0];
            q[m][1] = a[1];
            m++;
        }
        if ((da >= 0.0f) != (db >= 0.0f) && m < VU1REF_POLY_MAX) {
            const float t = da / (da - db);
            q[m][0] = a[0] + (b[0] - a[0]) * t;
            q[m][1] = a[1] + (b[1] - a[1]) * t;
            m++;
        }
    }
    memcpy(poly, q, (size_t)m * sizeof(q[0]));
    return m;
}

int vu1ref_polygon_reaches_picture(const float (*poly)[2], int n, const float pic[4])
{
    float p[VU1REF_POLY_MAX][2];
    if (n < 3 || n > VU1REF_POLY_MAX - 2) {
        return n >= 3; /* not a polygon this cuts: counted as reaching */
    }
    memcpy(p, poly, (size_t)n * sizeof(p[0]));
    /* vu_cut_reaches_picture: the picture's rows first, then the columns */
    n = cutPolygon(p, n, 1, pic[2], 1.0f);
    n = cutPolygon(p, n, 1, pic[3], -1.0f);
    if (n < 3) {
        return 0;
    }
    float lo = p[0][0], hi = p[0][0];
    for (int i = 1; i < n; i++) {
        lo = p[i][0] < lo ? p[i][0] : lo;
        hi = p[i][0] > hi ? p[i][0] : hi;
    }
    return lo < pic[1] && hi > pic[0];
}

int vu1ref_rect_reaches_picture(float x0, float y0, float x1, float y1, const float pic[4])
{
    /* vu_rect_reaches_picture */
    const float xl = x0 < x1 ? x0 : x1, xh = x0 < x1 ? x1 : x0;
    const float yl = y0 < y1 ? y0 : y1, yh = y0 < y1 ? y1 : y0;
    return xl < pic[1] && xh > pic[0] && yl < pic[3] && yh > pic[2];
}
