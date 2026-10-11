/*
 * port/game/demo_walk.c
 *
 * The opening's soldiers walk off the wide picture (issue 68); the rules
 * are in demo_walk.h.
 */
#include "demo_walk.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DW_POSE_EPS 1e-3f  /* a node key equal to the last frame's */
#define DW_ROOT_EPS 0.05f  /* a root equal to the last frame's */
#define DW_WALK_MIN 1.0f   /* a walk's peak speed, units a motion frame */
#define DW_SPEED_WINDOW 10 /* frames the speed is averaged over */
#define DW_PERIOD_MIN 4
#define DW_PERIOD_MAX 80
#define DW_PERIOD_DIP 0.35f /* a cycle's difference against the largest */
#define DW_HALF_43 256.0f   /* the 4:3 picture's half-width, pixels */
#define DW_PLANS 16
#define DW_ACTORS 8

static const char *const s_listed[] = {
    "D1_C18_SA", "D1_C18_SB", "D1_C18_SC", "D1_C23_SA", "D1_C23_SB", "D1_C23_SC",
};

int ico_demo_walk_listed(const char *motion_name)
{
    size_t i;

    if (motion_name == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(s_listed) / sizeof(s_listed[0]); i++) {
        if (strcmp(motion_name, s_listed[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static float hspeed(const float *a, const float *b)
{
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];

    return sqrtf(dx * dx + dz * dz);
}

/* the mean absolute difference of two frames' node keys */
static float poseDiff(const float *pose, int nodes, int f, int g)
{
    const float *a = pose + (size_t)f * (size_t)nodes * 3;
    const float *b = pose + (size_t)g * (size_t)nodes * 3;
    float s = 0.0f;
    int i;

    for (i = 0; i < nodes * 3; i++) {
        s += fabsf(a[i] - b[i]);
    }
    return s / (float)(nodes * 3);
}

/* 1 when frame f's keys all equal frame g's */
static int sameKeys(const float *pose, const float *root, int nodes, int f, int g)
{
    const float *a = pose + (size_t)f * (size_t)nodes * 3;
    const float *b = pose + (size_t)g * (size_t)nodes * 3;
    int i;

    for (i = 0; i < 3; i++) {
        if (fabsf(root[f * 3 + i] - root[g * 3 + i]) >= DW_ROOT_EPS) {
            return 0;
        }
    }
    for (i = 0; i < nodes * 3; i++) {
        if (fabsf(a[i] - b[i]) >= DW_POSE_EPS) {
            return 0;
        }
    }
    return 1;
}

static int plan(IcoDemoWalkPlan *p, int frames, int nodes, const float *root, const float *pose)
{
    const int last = frames - 1;
    float peak = 0.0f;
    float sum = 0.0f;
    float d[DW_PERIOD_MAX + 1];
    float runmax = 0.0f;
    int tail = last;
    int stop = -1;
    int top;
    int period = 0;
    int f;
    int q;

    while (tail > 0 && sameKeys(pose, root, nodes, tail - 1, last)) {
        tail--;
    }
    p->tail = tail;

    /* the walk's speed: the peak of the 10-frame average before the tail */
    top = tail < last ? tail : last;
    for (f = 0; f < top; f++) {
        int k0 = f + 1 - DW_SPEED_WINDOW < 0 ? 0 : f + 1 - DW_SPEED_WINDOW;
        int k;

        sum = 0.0f;
        for (k = k0; k <= f; k++) {
            sum += hspeed(&root[(k + 1) * 3], &root[k * 3]);
        }
        if (sum / (float)(f + 1 - k0) > peak) {
            peak = sum / (float)(f + 1 - k0);
        }
    }
    if (peak < DW_WALK_MIN) {
        return 0;
    }
    for (f = 0; f < top; f++) {
        if (hspeed(&root[(f + 1) * 3], &root[f * 3]) >= 0.5f * peak) {
            stop = f + 1;
        }
    }
    if (stop < 0) {
        return 0;
    }
    p->stop = stop;

    /* the stride: two whole cycles before the stop */
    for (q = DW_PERIOD_MIN; q <= DW_PERIOD_MAX && stop - 2 * q >= 0; q++) {
        float s = 0.0f;

        for (f = stop - q; f < stop; f++) {
            s += poseDiff(pose, nodes, f, f - q);
        }
        d[q] = s / (float)q;
    }
    for (f = DW_PERIOD_MIN; f < q; f++) {
        if (d[f] > runmax) {
            runmax = d[f];
        }
        if (f > DW_PERIOD_MIN && f + 1 < q && d[f] <= d[f - 1] && d[f] <= d[f + 1] &&
            d[f] <= DW_PERIOD_DIP * runmax) {
            period = f;
            break;
        }
    }
    if (period == 0) {
        return 0;
    }
    p->period = period;
    p->vel[0] = (root[stop * 3 + 0] - root[(stop - period) * 3 + 0]) / (float)period;
    p->vel[1] = 0.0f;
    p->vel[2] = (root[stop * 3 + 2] - root[(stop - period) * 3 + 2]) / (float)period;
    p->root[0] = root[stop * 3 + 0];
    p->root[1] = root[stop * 3 + 1];
    p->root[2] = root[stop * 3 + 2];
    p->valid = 1;
    return 1;
}

int ico_demo_walk_analyse(IcoDemoWalkPlan *p, int frames, int nodes, const void *data,
                          IcoDemoWalkRootFn root_at, IcoDemoWalkPoseFn pose_at)
{
    float *root;
    float *pose;
    int ok;
    int f;
    int n;

    memset(p, 0, sizeof(*p));
    if (frames < 4 || nodes < 1 || root_at == NULL || pose_at == NULL) {
        return 0;
    }
    root = malloc((size_t)frames * 3 * sizeof(float));
    pose = malloc((size_t)frames * (size_t)nodes * 3 * sizeof(float));
    if (root == NULL || pose == NULL) {
        free(root);
        free(pose);
        return 0;
    }
    for (f = 0; f < frames; f++) {
        root_at(data, f, &root[f * 3]);
        for (n = 0; n < nodes; n++) {
            pose_at(data, f, n, &pose[((size_t)f * (size_t)nodes + (size_t)n) * 3]);
        }
    }
    ok = plan(p, frames, nodes, root, pose);
    if (!ok) {
        p->valid = 0;
    }
    free(root);
    free(pose);
    return ok;
}

int ico_demo_walk_should_start(float dx, float dy, int depth_ok, int native_w, int native_h)
{
    const float hw = (float)native_w * 0.5f;
    const float hh = (float)native_h * 0.5f;

    if (!depth_ok || fabsf(dy) > hh) {
        return 0;
    }
    return fabsf(dx) > DW_HALF_43 && fabsf(dx) <= hw;
}

int ico_demo_walk_should_stop(float dx, float dy, int depth_ok, int native_w, int native_h)
{
    const float hw = (float)native_w * 0.5f + ICO_DEMO_WALK_MARGIN;
    const float hh = (float)native_h * 0.5f + ICO_DEMO_WALK_MARGIN;

    return !depth_ok || fabsf(dx) > hw || fabsf(dy) > hh;
}

float ico_demo_walk_update(IcoDemoWalkActor *a, const IcoDemoWalkPlan *p, float frame, float play,
                           float dx, float dy, int depth_ok, int native_w, int native_h)
{
    if (!p->valid || frame < (float)p->stop) {
        a->state = ICO_DEMO_WALK_IDLE;
        a->e = 0.0f;
        return -1.0f;
    }
    if (a->state == ICO_DEMO_WALK_IDLE) {
        if (!ico_demo_walk_should_start(dx, dy, depth_ok, native_w, native_h)) {
            a->state = ICO_DEMO_WALK_DECLINED;
            return -1.0f;
        }
        a->state = ICO_DEMO_WALK_WALKING;
        a->play0 = play - (frame - (float)p->stop);
        a->e = 0.0f;
    }
    if (a->state == ICO_DEMO_WALK_WALKING) {
        float e = play - a->play0;

        if (e > a->e) {
            a->e = e;
        }
        if (a->e >= ICO_DEMO_WALK_MAX_FRAMES ||
            ico_demo_walk_should_stop(dx, dy, depth_ok, native_w, native_h)) {
            a->state = ICO_DEMO_WALK_HELD;
        }
        return a->e;
    }
    if (a->state == ICO_DEMO_WALK_HELD) {
        return a->e;
    }
    return -1.0f;
}

float ico_demo_walk_pose(const IcoDemoWalkPlan *p, float e, float *root_out3)
{
    const float period = (float)p->period;
    float k = fmodf(e, period);

    if (k < 0.0f) {
        k += period;
    }
    root_out3[0] = p->root[0] + p->vel[0] * e;
    root_out3[1] = p->root[1];
    root_out3[2] = p->root[2] + p->vel[2] * e;
    return (float)(p->stop - p->period) + k;
}

/* --- the game's hook: the plans and the actors ----------------------------- */

typedef struct DwCached {
    int used;
    int motion;
    const void *data;
    int frames;
    int nodes;
    IcoDemoWalkPlan plan;
} DwCached;

static DwCached s_plans[DW_PLANS];
static int s_planNext;
static IcoDemoWalkActor s_actors[DW_ACTORS];
static int s_actorNext;

static const IcoDemoWalkPlan *planFor(int motion, const void *data, int frames, int nodes,
                                      IcoDemoWalkRootFn root_at, IcoDemoWalkPoseFn pose_at)
{
    DwCached *c;
    int i;

    for (i = 0; i < DW_PLANS; i++) {
        c = &s_plans[i];
        if (c->used && c->motion == motion && c->data == data && c->frames == frames &&
            c->nodes == nodes) {
            return &c->plan;
        }
    }
    c = &s_plans[s_planNext];
    s_planNext = (s_planNext + 1) % DW_PLANS;
    c->used = 1;
    c->motion = motion;
    c->data = data;
    c->frames = frames;
    c->nodes = nodes;
    ico_demo_walk_analyse(&c->plan, frames, nodes, data, root_at, pose_at);
    return &c->plan;
}

static IcoDemoWalkActor *actorFor(const void *actor, int motion)
{
    IcoDemoWalkActor *a;
    int i;

    for (i = 0; i < DW_ACTORS; i++) {
        a = &s_actors[i];
        if (a->actor == actor) {
            if (a->motion != motion) {
                a->motion = motion;
                a->state = ICO_DEMO_WALK_IDLE;
                a->e = 0.0f;
            }
            return a;
        }
    }
    a = &s_actors[s_actorNext];
    s_actorNext = (s_actorNext + 1) % DW_ACTORS;
    memset(a, 0, sizeof(*a));
    a->actor = actor;
    a->motion = motion;
    return a;
}

int ico_demo_walk_tick(const void *actor, int motion, const char *motion_name, const void *data,
                       int frames, int nodes, IcoDemoWalkRootFn root_at, IcoDemoWalkPoseFn pose_at,
                       float frame, float play, float dx, float dy, int depth_ok, int native_w,
                       int native_h, float *pose_frame, float *root_out3)
{
    const IcoDemoWalkPlan *p;
    IcoDemoWalkActor *a;
    float e;

    if (!ico_demo_walk_listed(motion_name) || data == NULL) {
        return 0;
    }
    p = planFor(motion, data, frames, nodes, root_at, pose_at);
    if (!p->valid) {
        return 0;
    }
    a = actorFor(actor, motion);
    e = ico_demo_walk_update(a, p, frame, play, dx, dy, depth_ok, native_w, native_h);
    if (e < 0.0f) {
        return 0;
    }
    *pose_frame = ico_demo_walk_pose(p, e, root_out3);
    return 1;
}

void ico_demo_walk_reset(void)
{
    memset(s_plans, 0, sizeof(s_plans));
    memset(s_actors, 0, sizeof(s_actors));
    s_planNext = 0;
    s_actorNext = 0;
}
