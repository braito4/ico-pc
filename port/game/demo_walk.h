/*
 * port/game/demo_walk.h
 *
 * The opening's soldiers walk off the wide picture (issue 68).
 *
 * The opening's cutscene soldiers (DEMOMOTCTRL actors) play motions keyed
 * for the PS2's 4:3 camera.  Once a soldier had left the 4:3 picture the
 * animator stopped keying it: the root stops, the legs may walk on in place
 * for a while, then every key holds to the motion's end (cut 23's D1_C23_SA
 * and _SC), or the motion simply ends and holds its last frame while the
 * cut goes on (cut 22 replays the 210-frame D1_C18 motions in a 270-frame
 * cut).  On the PS2 that was never seen; on a wider native frame the soldier
 * stands frozen beside the picture while the sword's own track goes on.
 *
 * The keys' intent is "the soldier walks off the picture".  For a listed
 * motion this module finds, once per motion:
 *   - the tail: the first frame from which every root and node key equals
 *     the last frame's (frames - 1 when the keys change to the end, the
 *     motion's own end hold);
 *   - the stop: the last frame before the tail at which the root still moves
 *     horizontally at half the walk's speed or more (the walk's speed is the
 *     peak of the 10-frame average of the horizontal root speed; a motion
 *     whose peak is under 1 unit a frame does not walk);
 *   - the walk cycle: the stride period P of the node keys over the frames
 *     before the stop (the first interior local minimum of the mean key
 *     difference between frames P apart that is at most 0.35 of the largest
 *     difference at shorter periods, P from 4 to 80, two whole cycles before
 *     the stop), the cycle being the P frames that end at the stop;
 *   - the velocity: the horizontal root travel over that cycle divided by P.
 * When the motion's frame first reaches the stop the actor is projected
 * through the camera: every node of the skeleton (the last tick's node
 * matrices), the box of their screen positions widened by
 * ICO_DEMO_WALK_SKIN of its height on each side (the body reaches past its
 * joints: the head's top, the fingers, the toes).  When that whole box is
 * outside the camera's 4:3 picture on one side (beyond 256 pixels from the
 * centre: no limb of the soldier is in the picture the PS2 showed, which
 * keeps his frozen pose), its near edge is inside the native frame (|x| <=
 * gsFrame.w / 2), the root is inside it vertically (|y| <= h / 2) and every
 * node is in front of the camera, the extension starts; otherwise the
 * motion's own keys play to the end as on the PS2.
 * While it runs, the walk cycle repeats and the root advances at the
 * velocity from the root at the stop, e motion frames after the stop (the
 * motion's play clock); once the root projects ICO_DEMO_WALK_MARGIN pixels
 * past the native frame's side or its top or bottom, or goes behind the
 * camera, or ICO_DEMO_WALK_MAX_FRAMES have passed, the actor holds that
 * pose and place.  A new motion, or the same motion played again from
 * before its stop, starts over.
 *
 * At 4:3 the native frame is the 4:3 picture, so no point is outside the
 * one and inside the other: the extension never starts and the game is the
 * PS2's.  Pure: no game headers; the game hands in the keys through the two
 * readers and the projected root; all of it is a function of the motion
 * data, the play clock and the camera, so a recording (which carries the
 * gs_width) replays the same.
 */
#ifndef ICO_PORT_GAME_DEMO_WALK_H
#define ICO_PORT_GAME_DEMO_WALK_H

/* pixels past the native frame's edge the root walks before the hold (the
   soldier's far side is still in the picture when the root crosses it) */
#define ICO_DEMO_WALK_MARGIN 128.0f
/* the projected joints' box is widened by this fraction of its height on
   each side for the decision (the skin past the joints) */
#define ICO_DEMO_WALK_SKIN 0.125f
/* motion frames the extension walks at most */
#define ICO_DEMO_WALK_MAX_FRAMES 600.0f

/* a motion's key readers: the root (x, y, z) and node `node`'s rotation
   (the quaternion's x, y, z) at whole frame `frame` */
typedef void (*IcoDemoWalkRootFn)(const void *data, int frame, float *out3);
typedef void (*IcoDemoWalkPoseFn)(const void *data, int frame, int node, float *out3);

/* what the keys say, ico_demo_walk_analyse's result */
typedef struct IcoDemoWalkPlan {
    int valid;     /* 1: the motion walks and has a walk cycle to repeat */
    int tail;      /* first frame of the unchanging tail */
    int stop;      /* the frame the extension takes over at */
    int period;    /* P, the walk cycle's length in frames */
    float vel[3];  /* horizontal root velocity a motion frame (y 0) */
    float root[3]; /* the root at the stop */
} IcoDemoWalkPlan;

/* the extension's state for one actor */
enum {
    ICO_DEMO_WALK_IDLE = 0, /* before the stop, or not yet decided */
    ICO_DEMO_WALK_WALKING,  /* the extension drives the actor */
    ICO_DEMO_WALK_DECLINED, /* decided at the stop: the keys play */
    ICO_DEMO_WALK_HELD      /* walked off: holds the last extended pose */
};

typedef struct IcoDemoWalkActor {
    const void *actor;
    int motion;
    int state;
    float play0; /* the play clock at the stop */
    float e;     /* motion frames walked past the stop */
} IcoDemoWalkActor;

/* 1 for the motions the extension is for: the opening's soldiers in cuts
   18 and 23 (D1_C18_SA/SB/SC, replayed in cut 22, and D1_C23_SA/SB/SC) */
int ico_demo_walk_listed(const char *motion_name);

/* Reads every key of a motion of `frames` frames and `nodes` nodes and
   fills *p as above; 0 (p->valid 0) when the motion has no walk to repeat. */
int ico_demo_walk_analyse(IcoDemoWalkPlan *p, int frames, int nodes, const void *data,
                          IcoDemoWalkRootFn root_at, IcoDemoWalkPoseFn pose_at);

/* 1 when a soldier whose body spans x0 .. x1 pixels across from the
   picture's centre, his root dy pixels down from it (depth_ok 0 when the
   root is behind the camera; x1 < x0, no span, when a node is), starts the
   extension: the whole span outside the 4:3 picture on one side, its near
   edge inside the native frame native_w x native_h.  Never at native_w
   512. */
int ico_demo_walk_should_start(float x0, float x1, float dy, int depth_ok, int native_w,
                               int native_h);

/* 1 when a walking root has gone: ICO_DEMO_WALK_MARGIN past the native
   frame's edge, or behind the camera */
int ico_demo_walk_should_stop(float dx, float dy, int depth_ok, int native_w, int native_h);

/* One geometry tick of actor state *a for plan *p at motion frame `frame`
   and play clock `play`, the root projected at dx, dy and the body across
   x0 .. x1 (the start decision's span).  Returns the motion frames past the
   stop the extension plays (>= 0), or -1 when the motion's own keys play. */
float ico_demo_walk_update(IcoDemoWalkActor *a, const IcoDemoWalkPlan *p, float frame, float play,
                           float dx, float dy, float x0, float x1, int depth_ok, int native_w,
                           int native_h);

/* What to play e frames past the stop: the pose frame in the walk cycle and
   the root (out3). */
float ico_demo_walk_pose(const IcoDemoWalkPlan *p, float e, float *root_out3);

/* The game's hook (ico2/sugipon/src/motionOrientManager.c): for a listed
   motion, the cached plan (analysed on first use) and the actor's state, one
   update.  Returns 1 when the extension drives the actor this tick, with
   *pose_frame and root_out3 to play; 0 when the motion's keys play. */
int ico_demo_walk_tick(const void *actor, int motion, const char *motion_name, const void *data,
                       int frames, int nodes, IcoDemoWalkRootFn root_at, IcoDemoWalkPoseFn pose_at,
                       float frame, float play, float dx, float dy, float x0, float x1,
                       int depth_ok, int native_w, int native_h, float *pose_frame,
                       float *root_out3);

/* forget every plan and actor state */
void ico_demo_walk_reset(void);

#endif /* ICO_PORT_GAME_DEMO_WALK_H */
