// vu_particle.hlsl: particle (RD_PROG_PARTICLE), START_PARTICLE
// (particle.vsm:79-149) for prim_DispParticle. The stream holds the batch as
// the VU gets it (PrimParticleObj from num on): qw 0 = count, 1..2 GIF tags,
// 3 clip minimum, 4 clip maximum, 5 = (size scale, du, dv, -), then two
// qwords per particle: (x, y, z, size), (u, v, grey, alpha). vu_draw.x =
// the batch's qw 0. Matrices (SET_PARTICLE_MATRIX): vu_mem[16..19] = model
// to GS screen (vf01..vf04), vu_mem[20..23] = the screen matrix
// (vf05..vf08). Drawn non-indexed, six vertices per particle: a GS sprite
// (PRIM 0xD6: TME, ABE, AA1) between the two corners, ST from (u, v) to
// (u + du, v + dv) with Q = 1, flat colour.
#include "vu_common.hlsli"

struct VuSprite {
    int3 c0; // ftoi4 of corner 0 (x - e, y - e) and Z
    int3 c1; // corner 1 (x + e, y + e)
    uint4 rgba;
    float4 st;  // u0, v0, u1, v1
    bool drawn; // the VU's test: alpha non-zero and both corners inside the clip window
    bool shown; // what the port can draw: alpha non-zero and w inside the window
};

VuSprite vu_particle(uint i)
{
    VuSprite o;
    uint base = vu_draw.x;
    float4 clipMin = vu_stream[base + 3u];
    float4 clipMax = vu_stream[base + 4u];
    float4 k = vu_stream[base + 5u];
    float4 a = vu_stream[base + 6u + 2u * i];
    float4 b = vu_stream[base + 7u + 2u * i];
    // :97-104 h = M * (x, y, z, 1)
    float4 h = vu_matm(16u, float4(a.xyz, 1.0), 1.0);
    // :99-112 e = S * (size * k.x, size * k.x, 0, 1): the half extent
    precise float sz = (0.0 + a.w) * k.x;
    float4 e = vu_matm(20u, float4(sz, sz, 0.0, 1.0), 1.0);
    // :108 div; :116-121 corners h -/+ e on xy, xyz divided, w stays h.w
    float q = vu_rcp(h.w);
    precise float3 p0 = float3(h.xy - e.xy, h.z) * q;
    precise float3 p1 = float3(h.xy + e.xy, h.z) * q;
    // :125-133 both corners strictly inside the window on x, y and w;
    // :100-105 a zero alpha skips the particle
    bool alphaZero = (asuint(b.w) & 0x7F800000u) == 0u;
    o.drawn = !alphaZero && vu_inside(float4(p0, h.w), clipMin, clipMax) &&
              vu_inside(float4(p1, h.w), clipMin, clipMax);
    // VIS-RULES item 10 (v0.6.1): parity out of the box, drawn beside it
    // and outside the out-of-the-box state (VIS-WINDOW, vu_particle_vs).
    // The port keeps the alpha test and the w window (the particle in
    // front of the eye and before the far limit); a sprite outside the x/y
    // window (1024..3071, prim_InitParticleByPartition) is dropped when its
    // quad reaches the camera's picture, as the triangles' region test is,
    // and drawn whole beside it (vu_particle_vs). That window is fixed in
    // GS pixels, so on a wide frame it ends at the frame's edge at 48:9 and
    // inside it at 60:9, where a sprite with a corner past it vanished whole.
    o.shown = !alphaZero && clipMin.w < h.w && h.w < clipMax.w;
    o.c0 = int3(vu_ftoi4(p0.x), vu_ftoi4(p0.y), vu_ftoi4(p0.z));
    o.c1 = int3(vu_ftoi4(p1.x), vu_ftoi4(p1.y), vu_ftoi4(p1.z));
    // :118-124 RGBA = ftoi0(grey, grey, grey, alpha)
    o.rgba = vu_rgba(float4(b.z, b.z, b.z, b.w));
    // :106-123 ST0 = (u, v, 1), ST1 = (u + du, v + dv, 1)
    precise float2 st1 = b.xy + k.yz;
    o.st = float4(b.xy, st1);
    return o;
}

uint vu_particle_probe(VuSprite s, uint field)
{
    uint r = 0u;
    if (field == 0u) {
        r = asuint(s.c0.x);
    } else if (field == 1u) {
        r = asuint(s.c0.y);
    } else if (field == 2u) {
        r = asuint(s.c1.x);
    } else if (field == 3u) {
        r = asuint(s.c1.y);
    } else if (field == 4u) {
        r = asuint(s.c0.z);
    } else if (field == 5u) {
        r = s.rgba.x | (s.rgba.y << 8) | (s.rgba.z << 16) | (s.rgba.w << 24);
    } else if (field == 6u) {
        r = asuint(s.st.x);
    } else if (field == 7u) {
        r = asuint(s.st.y);
    } else if (field == 8u) {
        r = asuint(s.st.z);
    } else if (field == 9u) {
        r = asuint(s.st.w);
    } else if (field == 10u) {
        r = s.drawn ? 1u : 0u;
    } else if (field == 11u) {
        r = asuint(s.c1.z);
    } else if (field == 12u) {
        r = s.shown ? 1u : 0u;
    }
    return r;
}

// Issue 76's rule for the x/y window (vu_common.hlsli vu_region_rule):
// whether a tick showed sprite i, seen through a camera blended between
// two ticks' (VU_F_TICKS): at tick k's camera (h_k = R_k h, vu_ticks; the
// half extent e is the screen matrix's, the same at every camera) the VU
// draws it (alpha, both corners inside the x/y window, w inside its
// window) and its quad reaches the frame (vu_frame_ndc).
bool vu_particle_tick_shown(uint i)
{
    uint base = vu_draw.x;
    float4 clipMin = vu_stream[base + 3u];
    float4 clipMax = vu_stream[base + 4u];
    float4 k = vu_stream[base + 5u];
    float4 a = vu_stream[base + 6u + 2u * i];
    float4 h = vu_matm(16u, float4(a.xyz, 1.0), 1.0);
    precise float sz = (0.0 + a.w) * k.x;
    float4 e = vu_matm(20u, float4(sz, sz, 0.0, 1.0), 1.0);
    float4 frame = vu_frame_ndc();
    [loop] for (uint t = 0u; t < 2u; t++)
    {
        uint b = 4u * t;
        precise float4 hk = vu_ticks[b] * h.x + vu_ticks[b + 1u] * h.y + vu_ticks[b + 2u] * h.z +
                            vu_ticks[b + 3u] * h.w;
        float q = vu_rcp(hk.w);
        precise float3 p0 = float3(hk.xy - e.xy, hk.z) * q;
        precise float3 p1 = float3(hk.xy + e.xy, hk.z) * q;
        if (!vu_inside(float4(p0, hk.w), clipMin, clipMax) ||
            !vu_inside(float4(p1, hk.w), clipMin, clipMax)) {
            continue;
        }
        float2 n0 = vu_ndc(float2(vu_ftoi4(p0.x), vu_ftoi4(p0.y)) * (1.0 / 16.0));
        float2 n1 = vu_ndc(float2(vu_ftoi4(p1.x), vu_ftoi4(p1.y)) * (1.0 / 16.0));
        float2 lo = min(n0, n1);
        float2 hi = max(n0, n1);
        if (lo.x < frame.y && hi.x > frame.x && lo.y < frame.z && hi.y > frame.w) {
            return true;
        }
    }
    return false;
}

VuVSOut vu_particle_vs(uint vid : SV_VertexID)
{
    if ((vu_draw.z & VU_F_PROBE) != 0u) {
        VuSprite s = vu_particle(vu_probe_vertex(vid));
        return vu_probe_out(vid, vu_particle_probe(s, (vid / 3u) % VU_PROBE_FIELDS));
    }
    VuSprite s = vu_particle(vid / 6u);
    VuVSOut o = vu_out_init();
    if (!s.shown) {
        return o;
    }
    // A sprite the PS2 skips for its x/y window, as the region test's
    // triangles (vu_region_rule): out of the box not drawn when its quad
    // reaches into the camera's picture (its columns and its rows:
    // vu_rect_reaches_picture), drawn whole (the GPU clips the quad) when
    // wholly beside it, under photo mode's free camera, and in any other
    // state (VU_F_EXTENDED, VIS-WINDOW: a sprite is an effect, never a card
    // parked at the picture's edge; the shown test keeps it in front of the
    // eye). Out of the box between two ticks (VU_F_TICKS): drawn whole
    // when a tick showed it (vu_particle_tick_shown), as a triangle the
    // region rule drops at the blended camera is (vu_region_rule).
    if (!s.drawn && (vu_draw.z & (VU_F_BEHIND_EYE | VU_F_EXTENDED)) == 0u) {
        float2 p0 = vu_ndc(float2(s.c0.xy) * (1.0 / 16.0));
        float2 p1 = vu_ndc(float2(s.c1.xy) * (1.0 / 16.0));
        if (vu_rect_reaches_picture(min(p0, p1), max(p0, p1)) &&
            ((vu_draw.z & VU_F_TICKS) == 0u || !vu_particle_tick_shown(vid / 6u))) {
            return o;
        }
    }
    // corners of the two triangles: (0,0) (1,0) (0,1), (0,1) (1,0) (1,1)
    uint c = vid % 6u;
    bool right = c == 1u || c == 4u || c == 5u;
    bool down = c == 2u || c == 3u || c == 5u;
    int3 g = int3(right ? s.c1.x : s.c0.x, down ? s.c1.y : s.c0.y, s.c1.z);
    o.pos = vu_gs_position(g, false);
    o.col = float4(s.rgba);
    o.stq = float3(right ? s.st.z : s.st.x, down ? s.st.w : s.st.y, 1.0);
    // the sprite's planes (vu_common.hlsli vu_attr_planes): S runs across,
    // T down, the same on both triangles
    float2 p0 = vu_texel_offset_px(float2(s.c0.xy) * (1.0 / 16.0));
    float2 p1 = vu_texel_offset_px(float2(s.c1.xy) * (1.0 / 16.0));
    float4 col = float4(s.rgba);
    return vu_attr_planes_at(o, p0, float2(p1.x, p0.y), float2(p0.x, p1.y), col, col, col,
                             float3(s.st.xy, 1.0), float3(s.st.z, s.st.y, 1.0),
                             float3(s.st.x, s.st.w, 1.0));
}
