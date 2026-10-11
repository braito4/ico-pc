/* rd_dst_read.c: the exact GS blend, computed in the fragment shader from
 * the destination.
 *
 * Design (package GS-BLEND)
 * -------------------------
 * The goal: at Original 4:3 the frame chain matches PCSX2 pass by pass
 * within GS integer rounding.  The GS blends in integers,
 *     out = ((A - B) * C >> 7) + D,   A, B, D in {Cs, Cd, 0}, C in {As, Ad, FIX}
 * (arithmetic shift: toward minus infinity), 0x80 = 1.0 with no clamp of C,
 * then COLCLAMP (clamp to 0..255, or wrap to 8 bits), PABE (an As with its
 * MSB clear writes Cs unblended), FBA (the stored alpha's MSB set); the
 * alpha channel is never blended (As is stored); FBMSK masks channels.
 * The GPU's fixed-function blender computes Cs * F + Cd * (1 - F) in float
 * and rounds to nearest when it stores UNORM8.  For every LERP (ALPHA 0x44,
 * 0x64, 0x68 with As or FIX: the materials, the full-screen tint sprite
 * after the fog, fades, bars) that is one level above the GS on about half
 * the channels.  Measured on the forest's tint sprite (PCSX2 2.8.2 software
 * renderer, godrays oracle state 552, GS draw 480: RGBA 223 219 205 4, ALPHA
 * 0x44): the floor formula gives every one of PCSX2's 262144 pixels (its
 * rt0 and rt1 dumps); round to nearest on the same rt0 is one level up on
 * 50.0 % of the channels (pass mean +5.51 +5.22 +5.19 against PCSX2's
 * +4.98 +4.77 +4.68), the port measured +5.50 +5.36 +4.74 (its blender is
 * not exactly round-to-nearest either), and over a frame the error only
 * ever adds: the port ended 0.4 to 6.4 % brighter than PCSX2,
 * worst in dark scenes (build-host/tmp/pcsx2cmp/godrays/STATUS.md,
 * "Open (not fixed): LERP blend rounding").
 *
 * Options:
 *  (C) a bias before the blend (Cs - 0.5 level, or a factor nudged down):
 *      cannot be exact.  The blender's result is round(Cs*F + Cd*(1-F)),
 *      the GS's floor((Cs - Cd) * F) + Cd: the two differ by whether the
 *      fraction of (Cs - Cd) * F is at least one half, which depends on
 *      Cd, so no per-fragment change of Cs or F made without Cd fixes
 *      every destination; a negative bias source is also clamped to 0 by
 *      the UNORM output, and llvmpipe's own blend is not exact
 *      round-to-nearest either (STATUS.md).
 *  (B) the existing exact path, a copy of the destination per blended draw
 *      (blend_int's RGBA8_UINT ping-pong, fx_sprite_ps's snapshot): exact
 *      only while the primitives of one draw do not overlap (they read the
 *      copy, not each other), and every blended draw needs its own copy
 *      and its own pass.  The forest and title dumps (godrays lists
 *      p2-*.txt, rd_replay_tool --list) have 70 to 136 LERP-path draws a
 *      frame, most of them VU meshes whose triangles overlap (foliage,
 *      hair, the shadow-creature smoke): a copy of the target per draw is
 *      70 to 136 copies of SCENE a frame (at Enhanced 4K about 33 MB each)
 *      and still wrong where a mesh overlaps itself.  Kept for the
 *      staticBlur sprites, where it is exact (one sprite per record).
 *  (A) the destination read, chosen: the fragment shader reads its own
 *      pixel in rasterization order, computes gs_dst_channel (gs_math.hlsli)
 *      and stores the result, blending off.  Exact for any overlap, in the
 *      draw's own pass.  Two realisations (rhi.h RhiDstRead):
 *        storage     the target as a rasterizer-ordered view: D3D12 ROV
 *                    (RasterizerOrderedTexture2D, ROVsSupported with typed
 *                    UAV loads of RGBA8), Vulkan storage image under
 *                    VK_EXT_fragment_shader_interlock (DXC compiles the
 *                    same HLSL to PixelInterlockOrderedEXT).  The target is
 *                    no colour attachment then (D3D12 cannot hold one
 *                    resource as RTV and UAV), so a blended draw runs in a
 *                    pass of its own kind ("storage pass": the depth
 *                    attached, no colour; rd_replay.c dstPassMode).  Draws
 *                    group: the dumps switch between blended and other
 *                    draws 5 to 15 times a frame, so 10 to 30 extra passes
 *                    and barrier pairs.  The shader writes, so the depth
 *                    and stencil tests must run before it
 *                    ([earlydepthstencil]); a discard after an early depth
 *                    write would leave the Z the GS never writes, so a
 *                    draw that writes Z and can discard (alpha test, DATE)
 *                    is split: the colour without Z write, then a
 *                    depth-only pass, exact when its Z test is ALWAYS (the
 *                    3 such draws a frame in the dumps: list 3's DATE
 *                    sprites); under GEQUAL/GREATER the colour pass would
 *                    see a different depth than the GS (self-occlusion
 *                    within the draw), so those stay on the fixed-function
 *                    blend (logged once; none in the dumps).
 *        attachment  the colour attachment read as an input attachment in
 *                    rasterization order (Vulkan
 *                    VK_EXT/ARM_rasterization_order_attachment_access over
 *                    VK_KHR_dynamic_rendering_local_read): the tile-based
 *                    path (no pass split, no extra bandwidth on a tiler),
 *                    the depth, stencil, discard and write mask stay
 *                    fixed-function, so it has no restriction.  Every pass
 *                    on an RGBA8 target that can be read is begun
 *                    readable (RENDERING_LOCAL_READ); a pending clear is
 *                    recorded as its own pass before it rather than as the
 *                    readable pass's load op (the load op's ordering
 *                    against input attachment reads in the same pass is
 *                    not relied on).
 *      Preference: attachment, then storage, then the fixed-function blend.
 *
 * Capability matrix (vulkan.gpuinfo.org coverage, all platforms, read
 * 2026-10-11: interlock 26 %, rasterization-order access EXT 22 %, ARM 21 %,
 * local read 46 %; Mesa docs/features.txt for the Mesa drivers):
 *   Vulkan, desktop Mesa      RADV gfx9+ (the Steam Deck), ANV, lavapipe:
 *                             interlock -> storage.  lavapipe also has the
 *                             attachment read (taken first); Turnip, panvk
 *                             v10+: attachment read.  RADV and ANV have no
 *                             rasterization-order attachment access.
 *   Vulkan, NVIDIA / AMD /    interlock where the driver exposes it
 *   Intel Windows drivers     (storage); not verified per driver here
 *                             (the gpuinfo device tables did not load):
 *                             the start-up log says what was found.
 *   Vulkan, Android           the attachment read on drivers with the
 *                             ARM or EXT extension and local read (Turnip
 *                             per Mesa; Arm's Mali driver where it exposes
 *                             both, not verified here); else
 *                             fixed-function.
 *   D3D12                     ROV (ROVsSupported: required at feature
 *                             level 12_1, which WARP reports; optional
 *                             below it) -> storage.
 *   anything else             fixed-function, as before (logged
 *                             "fixed-function (rounds up)").
 *
 * What goes through it: every pass of rd_replay.c's screen prims (sprites,
 * strips, the full-screen tint, fades, bars, letterbox; sprite_ps,
 * sprite_texa_ps, sprite_aa1_ps, sprite_stq_ps, font_ps, font_sheet_ps), the
 * VU meshes (vu_ps, vu_texa_ps) and the fog sprite (fog_lut_ps,
 * fog_lut_buffer_ps) whose fixed-function blend is not the GS's (dstWant,
 * rd_pipeline.c): the LERPs (modes 2, 4, 7), Cd*FIX + Cs (3, FIX past 0x80
 * too), the Ad forms (8 to 10, which read Ad/255 in hardware), Cd*As + Cd
 * (11, not representable at all), PABE on the premultiplied forms, and
 * COLCLAMP 0, on RGBA8 targets.  The premultiplied adds and subtracts (0,
 * 1, 5, 6: shader (Cs*F)>>7, ONE/ONE) are exact in hardware already and
 * stay there.  Left on the fixed-function blend: the MSAA epoch's passes
 * (Enhanced only, never Original: a multisampled storage image or input
 * attachment is not asked for, and the resolve's mean is no GS arithmetic
 * anyway), targets other than RGBA8, the port's presentation overlay (not
 * the game's GS), the storage path's Z-writing discarding draws under
 * GEQUAL/GREATER (above).  The staticBlur sprites and blend_int keep their
 * own exact paths.  DATE is unchanged: the snapshot is read as before.
 * The Enhanced preset takes the same path: the rounding is not a 4:3 or
 * resolution matter.
 *
 * Expected effect: the forest tint pass +4.98 +4.77 +4.68 exactly (the
 * floor formula matches all of PCSX2's pixels, gs_math_test), and the
 * upward drift of every LERP gone from the whole frame.
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

const char *rd__blend_exact_name(int mode)
{
    switch (mode) {
    case RD_BLEND_EXACT_STORAGE:
        return rhi_backend() == RHI_BACKEND_D3D12 ? "exact (rov)" : "exact (interlock)";
    case RD_BLEND_EXACT_ATTACHMENT:
        return "exact (attachment read)";
    default:
        return "fixed-function (rounds up)";
    }
}

bool rd__fs_has_dst(uint8_t fs)
{
    switch (fs) {
    case RD_FS_SPRITE:
    case RD_FS_SPRITE_TEXA:
    case RD_FS_SPRITE_AA1:
    case RD_FS_SPRITE_STQ:
    case RD_FS_FONT:
    case RD_FS_FONT_SHEET:
    case RD_FS_FOG:
    case RD_FS_FOG_BUFFER:
    case RD_FS_VU:
    case RD_FS_VU_TEXA:
        return true;
    default:
        return false;
    }
}

/* the modes whose shaders and layout rd__gpu_init made (every twin) */
static uint8_t madeModes(void)
{
    uint8_t m = 1u << RD_BLEND_EXACT_OFF;
    bool rov = g_rd.layoutDstStorage.id != 0, att = g_rd.layoutDstAttach.id != 0;
    for (int i = 0; i < RD_FS_COUNT; i++) {
        if (rd__fs_has_dst((uint8_t)i)) {
            rov = rov && g_rd.fsDstRov[i].id;
            att = att && g_rd.fsDstAtt[i].id;
        }
    }
    if (rov) {
        m |= 1u << RD_BLEND_EXACT_STORAGE;
    }
    if (att) {
        m |= 1u << RD_BLEND_EXACT_ATTACHMENT;
    }
    return m;
}

void rd__dst_init(void)
{
    g_rd.dstModes = g_rd.hasDevice ? madeModes() : (uint8_t)(1u << RD_BLEND_EXACT_OFF);
    int want = RD_BLEND_EXACT_OFF;
    if (g_rd.dstModes & (1u << RD_BLEND_EXACT_ATTACHMENT)) {
        want = RD_BLEND_EXACT_ATTACHMENT;
    } else if (g_rd.dstModes & (1u << RD_BLEND_EXACT_STORAGE)) {
        want = RD_BLEND_EXACT_STORAGE;
    }
    const char *e = getenv("ICO_RD_BLEND");
    if (e && e[0]) {
        int asked = -1;
        if (strcmp(e, "fixed") == 0 || strcmp(e, "off") == 0 || strcmp(e, "0") == 0) {
            asked = RD_BLEND_EXACT_OFF;
        } else if (strcmp(e, "storage") == 0 || strcmp(e, "interlock") == 0 ||
                   strcmp(e, "rov") == 0) {
            asked = RD_BLEND_EXACT_STORAGE;
        } else if (strcmp(e, "attachment") == 0) {
            asked = RD_BLEND_EXACT_ATTACHMENT;
        }
        if (asked < 0 || !(g_rd.dstModes & (1u << asked))) {
            rd__log("blend: ICO_RD_BLEND=%s %s; %s", e,
                    asked < 0 ? "is not fixed, storage or attachment" : "is not on this device",
                    rd__blend_exact_name(want));
        } else {
            want = asked;
        }
    }
    g_rd.dstMode = (uint8_t)want;
    rd__log("blend: %s", rd__blend_exact_name(want));
}

bool rd_set_blend_exact(RdBlendExact mode)
{
    if ((unsigned)mode >= RD_BLEND_EXACT_COUNT || !(g_rd.dstModes & (1u << mode))) {
        return false;
    }
    if (g_rd.dstMode != (uint8_t)mode) {
        rd__log("blend: %s", rd__blend_exact_name(mode));
    }
    g_rd.dstMode = (uint8_t)mode;
    return true;
}

RdBlendExact rd_blend_exact(void)
{
    return (RdBlendExact)g_rd.dstMode;
}

uint32_t rd__dst_target_usage(RhiFormat fmt, uint8_t *dstUsage)
{
    uint32_t usage = 0;
    uint8_t u = 0;
    /* every mode the device has, whichever is in force: rd_set_blend_exact
     * switches without remaking the targets */
    if (fmt == RHI_FMT_RGBA8_UNORM) {
        if (g_rd.dstModes & (1u << RD_BLEND_EXACT_STORAGE)) {
            usage |= RHI_TEX_STORAGE;
            u |= 1u;
        }
        if (g_rd.dstModes & (1u << RD_BLEND_EXACT_ATTACHMENT)) {
            usage |= RHI_TEX_INPUT_ATTACHMENT;
            u |= 2u;
        }
    }
    if (dstUsage) {
        *dstUsage = u;
    }
    return usage;
}

/* the fragment may be discarded after the depth test: the alpha test
 * (ate in modeZ), DATE, the fog's own depth test in the shader */
static bool passDiscards(const RdDrawPass *p)
{
    return (p->modeZ & (1u << 8)) != 0 || (p->flags & ICO_DF_DATE) != 0 || p->key.fs == RD_FS_FOG ||
           p->key.fs == RD_FS_FOG_BUFFER;
}

/* the flags that only the fixed-function paths read */
#define BLENDER_FLAGS                                                                              \
    ((uint32_t)ICO_DF_FIX_FACTOR | (uint32_t)ICO_DF_PREMUL | (uint32_t)ICO_DF_C1_DST |             \
     (uint32_t)ICO_DF_NODUAL_FACTOR | (uint32_t)ICO_DF_NODUAL_ALPHA_PASS)

static RdDrawPass dstPass(const RdDrawPass *p, int mode)
{
    RdDrawPass c = *p;
    c.key.dst =
        (uint8_t)(mode == RD_BLEND_EXACT_STORAGE ? RHI_DST_READ_STORAGE : RHI_DST_READ_ATTACHMENT);
    c.key.gs.blend = RD_BLEND_COUNT; /* blending off: the shader blends */
    c.key.gs.nodual = 0;
    c.flags &= ~(BLENDER_FLAGS | (15u << ICO_DF_WMASK_SHIFT));
    c.flags |= (uint32_t)(c.key.gs.colorMask & 15u) << ICO_DF_WMASK_SHIFT;
    c.fix = c.fixGs; /* FIX past 0x80 overshoots on the GS */
    c.dstWant = 0;
    return c;
}

/* the depth-only twin of a pass: its Z test, Z write and discards, no
 * colour */
static RdDrawPass depthOnly(const RdDrawPass *p)
{
    RdDrawPass z = *p;
    z.key.gs.colorMask = 0;
    z.key.gs.blend = RD_BLEND_COUNT;
    z.flags &= ~BLENDER_FLAGS;
    z.dstWant = 0;
    return z;
}

static bool eligible(const RdPipeKeyInt *k)
{
    return k->dst == RHI_DST_READ_NONE && k->colorFmt == RHI_FMT_RGBA8_UNORM &&
           rd__fs_has_dst(k->fs) && (k->gs.colorMask & 7u) != 0 && k->gs.nodual == 0 &&
           k->gs.stencil == RD_STENCIL_OFF && k->gs.msaa <= 1;
}

int rd__expand_dst(const RdDrawPass *in, int n, int mode, RdDrawPass out[4])
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        const RdDrawPass *p = &in[i];
        if (mode == RD_BLEND_EXACT_OFF || !p->dstWant || !eligible(&p->key)) {
            out[m++] = *p;
            continue;
        }
        if (mode == RD_BLEND_EXACT_STORAGE && p->key.gs.zwrite == RD_ZWRITE_ON &&
            p->key.depthFmt != RHI_FMT_UNKNOWN && passDiscards(p)) {
            if (p->key.gs.ztst == RD_ZTST_ALWAYS || p->key.gs.ztst == RD_ZTST_NEVER) {
                /* Z ALWAYS: the colour pass (early tests, no Z write) and
                 * the depth pass admit the same fragments, and the depth
                 * pass leaves the last one's Z, as the GS */
                out[m] = dstPass(p, mode);
                out[m].key.gs.zwrite = RD_ZWRITE_OFF;
                m++;
                out[m++] = depthOnly(p);
            } else {
                rd__log_once(RD_ONCE_DST_ZWRITE,
                             "blend: exact (storage): a draw that writes Z under Z GEQUAL or "
                             "GREATER and can discard stays on the fixed-function blend");
                out[m++] = *p;
            }
            continue;
        }
        out[m++] = dstPass(p, mode);
    }
    return m;
}

uint32_t rd__enumerate_dst(const RdPipeKeyInt *in, uint32_t n, RdPipeKeyInt *out, uint32_t max,
                           uint32_t count)
{
    const int mode = g_rd.dstMode;
    if (mode == RD_BLEND_EXACT_OFF) {
        return count;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (!eligible(&in[i]) || rd__blend_path(in[i].gs.blend) == RD_BP_NONE) {
            continue;
        }
        RdDrawPass p;
        memset(&p, 0, sizeof(p));
        p.key = in[i];
        p.dstWant = 1;
        /* a key keeps neither the alpha test nor DATE: the pass as it is,
         * and under the storage mode with Z write also split */
        RdDrawPass ex[4];
        int ne = rd__expand_dst(&p, 1, mode, ex);
        if (mode == RD_BLEND_EXACT_STORAGE && p.key.gs.zwrite == RD_ZWRITE_ON &&
            p.key.depthFmt != RHI_FMT_UNKNOWN) {
            ex[ne] = dstPass(&p, mode);
            ex[ne].key.gs.zwrite = RD_ZWRITE_OFF;
            ne++;
            ex[ne++] = depthOnly(&p);
        }
        for (int j = 0; j < ne; j++) {
            /* what the two-pass fallback makes of a non-dst image (the
             * depth-only pass under ICO_RD_NO_DUAL) */
            RdDrawPass nd[4];
            const int nn = rd__expand_no_dual(&ex[j], 1, nd);
            for (int k = 0; k < nn; k++) {
                count = rd__add_pipe_key(out, max, count, &nd[k].key);
            }
        }
    }
    return count;
}
