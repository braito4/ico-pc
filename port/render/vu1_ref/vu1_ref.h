/* vu1_ref.h: CPU references of ICO's five VU1 microprograms
 * (ico2/vusrc/{normal_c,normal_l,cluster,mesh,particle}.vsm).
 *
 * Each function does what the program's instructions do to one input batch
 * (the quadwords the VIF UNPACK put at TOP: a GIF tag, then the vertices)
 * and returns what the program writes to the GS: per vertex the PACKED ST,
 * RGBAQ and XYZ2 quadwords, with the ADC bit, in output order. They are test
 * oracles for port/shaders/vu_*.hlsl; rd_mesh.c keeps its per-list VU
 * images in a Vu1Ref and runs vu1ref_particle for the end-tag check.
 * The code below cites the VU1 instruction lines it follows.
 *
 * Float semantics: plain C float arithmetic in the caller's rounding mode
 * (the simulation thread runs round toward zero, port/platform/fpenv.c),
 * compiled with -ffp-contract=off so every multiply and add rounds on its
 * own as the VU's FMAC does; division, rsqrt and float to int go through
 * port/math/ps2float.h. Matrices are applied in the VU's accumulator order:
 * c0 * x, + c1 * y, + c2 * z, + c3 * w.
 *
 * State: the programs keep matrices and lights in VF registers between
 * MSCAL calls, read VU data memory the EE uploaded, and leave a few VI
 * registers behind that the next batch reads. Vu1Ref holds all three; the
 * upload functions are the SET_* routines (one per MSCAL code). */
#ifndef PORT_RENDER_VU1_REF_H
#define PORT_RENDER_VU1_REF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* VU1 data memory is 16 KB: 1024 quadwords. */
#define VU1_MEM_QW 1024

typedef struct Vu1Ref {
    float mem[VU1_MEM_QW][4]; /* VU1 data memory */
    float vf[32][4];          /* VF registers; vf[0] is (0, 0, 0, 1) */
    int32_t vi[16];           /* VI registers (16-bit on the VU; kept sign-extended) */
} Vu1Ref;

/* One vertex as the program sends it to the GS (GIF PACKED, regs ST, RGBAQ,
 * XYZ2). */
typedef struct VuGsVertex {
    float stq[4];    /* PACKED ST: S, T, Q; w is whatever the program left (ignored by the GS) */
    int32_t rgba[4]; /* PACKED RGBAQ as ftoi0 left it; the GS keeps bits 0..7 of each word */
    int32_t xyz[4];  /* PACKED XYZ2: X, Y (ftoi4; the GS keeps bits 0..15, 12.4), Z (32 bits),
                        w word: bit 15 is ADC (1 = no drawing kick) */
    /* not sent to the GS, for tests and debugging: */
    int32_t inside;     /* the loop's region test (1 when the program has none) */
    uint32_t clipFlags; /* scissor loops: clipw flags of mem[20..23] * pos (bits 0..5) */
} VuGsVertex;

static inline int vu_gs_adc(const VuGsVertex *v)
{
    return (v->xyz[3] >> 15) & 1;
}

/* The GS's view of the vertex: X and Y are 16-bit 12.4 values. */
static inline uint32_t vu_gs_x(const VuGsVertex *v)
{
    return (uint32_t)v->xyz[0] & 0xFFFFu;
}

static inline uint32_t vu_gs_y(const VuGsVertex *v)
{
    return (uint32_t)v->xyz[1] & 0xFFFFu;
}

/* A triangle fan the scissor programs (normal_c/normal_l code 36) kick from
 * SCISSOR_COMMON for one clipped triangle: GIF tag = VU memory 37 (a copy of
 * the common block's qw 3: PRIM 0x5D fan, IIP, TME, ABE) with NLOOP = n. */
#define VU_FAN_MAX 20

typedef struct VuGsFan {
    int n;
    int beforeVertex; /* kicked while the loop was at this output vertex, i.e. before
                         the batch's own packet, which is kicked after the loop */
    uint32_t tag[4];
    VuGsVertex v[VU_FAN_MAX];
} VuGsFan;

/* The output of one batch (one MSCNT). */
#define VU_BATCH_MAX 256
#define VU_FANS_MAX 128

typedef struct VuBatchOut {
    uint32_t tag[4]; /* the batch's GIF tag, copied from TOP + 0 */
    int count;       /* vertices in v */
    VuGsVertex v[VU_BATCH_MAX];
    int fanCount;
    VuGsFan fans[VU_FANS_MAX];
} VuBatchOut;

/* One sprite of the particle program: RGBAQ, then ST/XYZ2 for each corner. */
typedef struct VuGsSprite {
    int index; /* the particle's position in the input */
    int32_t rgba[4];
    float st[2][4];
    int32_t xyz[2][4];
} VuGsSprite;

#define VU_PARTICLES_MAX 80

typedef struct VuParticleOut {
    uint32_t tag[2][4]; /* the two GIF tags from the batch (NLOOP 1; the second with EOP) */
    int count;
    VuGsSprite s[VU_PARTICLES_MAX];
} VuParticleOut;

/* ------------------------------------------------------------- state */

void vu1ref_init(Vu1Ref *r);
/* gsb_MakeCommonMatrix's UNPACK of 16 qwords to VU memory 0..15
 * (RdVuCommon). */
void vu1ref_load_common(Vu1Ref *r, const float qw[16][4]);
/* Code 2, SET_UVOFFSET (vu1_common.h:57): mem[2].xy = qw.xy. */
void vu1ref_set_uv_offset(Vu1Ref *r, const float qw[4]);

/* normal_c / normal_l code 16, SET_NORMAL_MATRIX (normal_c.vsm:54): 12 qwords
 * to vf01..vf04 and mem[16..27]; mem[3] (the common GIF tag) to mem[37]. */
void vu1ref_normal_set_matrix(Vu1Ref *r, const float qw[12][4]);
/* normal_c / normal_l code 18, SET_NORMAL_LIGHT (normal_c.vsm:88): mem[28..35]. */
void vu1ref_normal_set_light(Vu1Ref *r, const float qw[8][4]);
/* cluster code 16, SET_CLUSTER_MATRIX (cluster.vsm:69): qw[0].x (int) is the
 * count n + 1 of qwords copied from qw[1] to mem[16..]; qw[0].w goes to
 * mem[2].w; then vf09..vf12 = mem[4..7]. qw must hold n + 2 qwords (the
 * program copies one past the unpack). */
void vu1ref_cluster_set_matrix(Vu1Ref *r, const float (*qw)[4]);
/* cluster code 18, SET_CLUSTER_LIGHT (cluster.vsm:98): vf13..vf20. */
void vu1ref_cluster_set_light(Vu1Ref *r, const float qw[8][4]);
/* mesh code 16, SET_MESH_MATRIX (mesh.vsm:85): vf01..vf04 and the clip
 * constants vf13 = 0, vf14.xyw = 4094, 4094, 16777214. */
void vu1ref_mesh_set_matrix(Vu1Ref *r, const float qw[4][4]);
/* mesh code 18, SET_MESH_LIGHT (mesh.vsm:102): vf05..vf12. */
void vu1ref_mesh_set_light(Vu1Ref *r, const float qw[8][4]);
/* particle code 16, SET_PARTICLE_MATRIX (particle.vsm:43): vf01..vf08. */
void vu1ref_particle_set_matrix(Vu1Ref *r, const float qw[8][4]);

/* ------------------------------------------------------- the port's rule
 * What the renderer draws of what the programs above output
 * (port/shaders/vu_common.hlsli vu_triangle_out_marked, vu_region_drops,
 * vu_content_hidden; vu_particle.hlsl vu_particle_vs).  The region test
 * itself is the program's own on the position as computed, at every
 * aspect (VuGsVertex.inside). */
enum {
    VU1REF_ORIGINAL = 0, /* the Original preset (FrameCB misc[1] 0): the PS2's picture */
    VU1REF_ENHANCED = 1
};

enum {
    VU1REF_DRAW_NONE = 0,   /* not drawn */
    VU1REF_DRAW_EXACT = 1,  /* drawn at its GS positions, as the GS draws it */
    VU1REF_DRAW_CLIPPED = 2 /* drawn, clipped by the GPU at the native frustum */
};

/* A strip triangle whose vertices' region tests are insideA..C: all inside,
 * drawn exactly; one outside: clipped under photo mode's free camera
 * (freeCamera, ICO_VU_BEHIND_EYE); else not drawn when reaches (what the
 * GPU draws of it, cut to 0 <= z <= w and to the picture's rows, meets the
 * camera's picture's columns: vu_cut_reaches_picture,
 * vu1ref_polygon_reaches_picture), as the PS2 does not draw it, and
 * clipped when it lies wholly beside it.  The same in both presets (preset
 * changes nothing here).  hide: the triangle carries ICO_VU_INDEX_HIDE
 * (never drawn). */
int vu1ref_region_draw(int preset, int freeCamera, int insideA, int insideB, int insideC, int hide,
                       int reaches);
/* A particle: shown (alpha non-zero, w inside the window) and drawn (the
 * VU's test, both corners inside its x/y window too).  Not drawn by the VU:
 * the whole sprite (the GPU clips it) under the free camera; else dropped
 * when its quad reaches the camera's picture (reaches,
 * vu1ref_rect_reaches_picture), drawn whole when
 * wholly beside it; the same in both presets. */
int vu1ref_particle_draw(int preset, int freeCamera, int drawn, int shown, int reaches);

/* The reach those two take (vu_common.hlsli vu_cut_reaches_picture,
 * vu_rect_reaches_picture). pic = the camera's picture: x0, x1, y0, y1
 * (left, right, top, bottom; GS (2048, 2048) -+ the picture's half-size
 * from FrameCB frame[1] and frame[2]: 256 by half the native height for the
 * frame's camera, a reflection camera's own), in the same pixels as the
 * points. A polygon: what the GPU draws of a triangle the region test
 * drops, cut at the near plane, in screen pixels (at most
 * VU1REF_POLY_MAX - 2 corners); it is cut to the picture's rows, then its
 * x extent met with the columns: a triangle whose part within the
 * columns lies wholly above or below the picture reaches nothing. A
 * sprite: its corners (x0, y0) and (x1, y1). Edges count as reaching. */
#define VU1REF_POLY_MAX 8
int vu1ref_polygon_reaches_picture(const float (*poly)[2], int n, const float pic[4]);
int vu1ref_rect_reaches_picture(float x0, float y0, float x1, float y1, const float pic[4]);

/* ------------------------------------------------------------- draws
 * in = the batch at TOP: in[0] is the GIF tag (NLOOP in the low 15 bits of
 * the first word is the vertex count), then the vertices. Each run is the
 * BEGIN entry (the MSCAL code) followed by its START loop (the MSCNT). */

/* normal_c (prelit): code 32 BEGIN_NORMAL_C (region test), 34
 * BEGIN_NORMAL_C_NOCLIP, 36 BEGIN_SCISSOR_C. Vertex: pos, ST, colour. */
void vu1ref_normal_c(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out);
/* normal_l (lit): code 32 BEGIN_NORMAL_L, 34 BEGIN_NORMAL_L_SPEC, 36
 * BEGIN_SCISSOR_L, 38 BEGIN_NORMAL_REF. Vertex: pos, normal, ST, colour. */
void vu1ref_normal_l(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out);
/* cluster (skinned): code 20 CLUSTER_0, 22 CLUSTER_0_SPEC, 24
 * CLUSTER_1_SPEC. Vertex: pos, normal, weights (int addr0, w0, int addr1,
 * w1), ST, colour. */
void vu1ref_cluster(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out);
/* mesh (procedural grid): code 20 MESH_NOLIGHT, 22 MESH_LIGHT, 24
 * MESH_LIGHT_SPEC. in[1] is the batch colour, then per vertex pos, [normal,]
 * ST. */
void vu1ref_mesh(Vu1Ref *r, int code, const float (*in)[4], VuBatchOut *out);
/* particle: code 18 BEGIN_PARTICLE (or the fall-through from code 16) then
 * START_PARTICLE. in[0].x = count, in[1], in[2] GIF tags, in[3] clip
 * minimum, in[4] clip maximum, in[5] (size scale, du, dv, -), then two qwords
 * per particle: (x, y, z, size), (u, v, grey, alpha).
 * inQw is the number of qwords readable at in: the count in[0].x is clamped to
 * (inQw - 6) / 2 particles, and an input shorter than the 6 header qwords
 * draws nothing (the game fills count from its own buffer, a corrupt or
 * over-large count must not read past it). */
void vu1ref_particle(Vu1Ref *r, const float (*in)[4], uint32_t inQw, VuParticleOut *out);

/* ------------------------------------------------------------- helpers */

/* The triangles a strip batch draws: the GS draws a triangle at every
 * vertex k whose XYZ2 has ADC clear, from vertices k-2, k-1, k (the GIF
 * tag's PRE resets the vertex queue at the batch start). Writes the k of each
 * drawn triangle to kicks (at most out->count) and returns their number. */
int vu1ref_kicks(const VuBatchOut *out, int *kicks);

/* The static part of the drawing kicks of a vertex stream, which rd's index
 * buffer encodes (port/render/rd_mesh.h): triangle k (vertices k-2..k) can
 * be drawn when k is at least the third vertex of its batch and, for the
 * programs that read the strip flag (normal_c, normal_l, cluster), neither
 * vertex k nor k-1 has ST.w < 1. stw = the ST.w of each vertex; NULL for the
 * mesh program (no flag). batchStart(k) is given as the first vertex index
 * of k's batch through batchFirst[k]. Returns the count written to kicks. */
int vu1ref_static_kicks(const float *stw, const int *batchFirst, int count, int *kicks);

#ifdef __cplusplus
}
#endif

#endif
