/*
 * port/game/photo_view.c
 *
 * Photo mode's camera in the game's matrices (photo_view.h).  Built with
 * the game's include path and record layout (it reads matrixptr, the pause
 * and the stage number), in the window and the headless build.
 */
#include <stdio.h>
#include <string.h>

#include "typedef.h"
#include "main.h"
#include "GsBase.h"
#ifdef ICO_RD
#include "DisplayList.h"
#include "RegistPacket.h"
#include "gobj.h"
#endif

#include "photo_mode.h"
#include "photo_view.h"
#include "rd.h"
#include "video_options.h"

static struct {
    int saved;
    int stage;      /* stage_no at enter */
    float d;        /* the game camera's distance (gsb_ViewFocus) */
    RdCamera game0; /* the game camera at enter */
    /* the 500 unit pair (matrixptr +0x640, +0x680) of the tick's full
       screen projection, for ico_photo_view_veil; set while pair is */
    float screen500[2][16] __attribute__((aligned(16)));
    int pair;
} v;

int ico_photo_view_saved(void)
{
    return v.saved;
}

static void enter(void)
{
    gsb_PushView();
    memset(&v.game0, 0, sizeof(v.game0));
    memcpy(v.game0.view, matrixptr + 0x80, sizeof(v.game0.view));
    memcpy(v.game0.proj, matrixptr + 0xC0, sizeof(v.game0.proj));
    v.game0.aspect = gsFrame.aspect; /* the frame's display aspect, as gsbHostCommon */
    v.d = gsb_ViewFocus();
    v.stage = stage_no;
    v.saved = 1;
    if (!ico_photo_set_game(&v.game0)) {
        fprintf(stderr, "photo: the game camera's view does not invert\n");
    }
}

static void leave(void)
{
    const int same = stage_no == v.stage && systemStatus[5] != 0;
    if (same) {
        /* the game's camera back, its common block and the renderer's
           camera rebuilt from it, then the save once more: after a
           reflection pass (puddle.c) +0x240 is at the reflection's size
           while +0x280 is still the full screen's, and the rebuild would
           replace that +0x280 (the cull's matrix) with the reflection's */
        gsb_PopView();
        gsb_MakeCommonMatrix();
        gsb_PopView();
    }
    v.saved = 0;
    v.pair = 0;
    ico_photo_set_game(NULL);
    ico_photo_set_left(1);
    /* the picture jumps back to the game camera: no blend into it */
    ico_video_camera_cut();
    if (!same) {
        fprintf(stderr, "photo: the stage changed or the game runs: its camera kept\n");
    }
}

#ifdef ICO_RD
/* 1 when the photo camera ov at zoom k is the game camera at enter: the
   same view matrix, no zoom */
static int photoIsGameCamera(const RdCamera *ov, float k)
{
    return k == 1.0f && memcmp(ov->view, v.game0.view, sizeof(ov->view)) == 0;
}
#endif

void ico_photo_view_tick(void)
{
    ico_photo_set_left(0);
    const int on = ico_photo_active() && systemStatus[5] != 0;
    if (v.saved && (!on || stage_no != v.stage)) {
        leave();
        return;
    }
    if (!on) {
        return;
    }
    if (!v.saved) {
        enter();
    }
    RdCamera ov;
    if (!ico_photo_camera(&ov, &v.game0)) {
        ov = v.game0;
    }
    IcoPhotoState st;
    ico_photo_get(&st);
    const float k = st.zoom > 0.0f ? st.zoom : 1.0f;
    memcpy(matrixptr + 0x80, ov.view, sizeof(ov.view));
    gsb_SetVSMatrix(ScreenWidth, ScreenHeight, v.d * k);
    /* the full screen's 500 unit pair, which a reflection pass drawn later
       in the frame replaces at its own size (puddle.c, pool.c) */
    memcpy(v.screen500, matrixptr + 0x640, sizeof(v.screen500));
    v.pair = 1;
    gsb_MakeCommonMatrix();
#ifdef ICO_RD
    /* the renderer keeps the triangles with a vertex behind this camera
       (RdCamera.freeCamera): the game's cameras rely on them being dropped,
       a free camera does not; the headless build has no renderer.  The
       game's own rule stays while the aspect sweep runs (its frames are
       the game's frame at each width, so a draw the game's rule drops
       beside the picture shows in the sweep as it does in play) and while
       the photo camera is still the game's camera (not moved, no zoom) */
    if (!ico_video_frame_forced() && !photoIsGameCamera(&ov, k)) {
        rd_free_camera();
    }
#endif
}

#ifdef ICO_RD
/* Photo mode's Deja vu (photo_mode.h ICO_PHOTO_FILM_DEJA): the deja
 * vision's grain veil over the photo, drawn as stage 40 draws it.
 *
 * The intent, from the game's data: stage 40 (deja) plays the stage
 * animation deja_lightning.bga (stageTable row 79, whose objTableScene range
 * holds the model 1614, sandstorm.p2o, of object kind 35), whose two
 * "sandstorm" entries (type 10, nodes 13 and 14 under CAMERA_PARENT) hold
 * the model in the camera's space (BgAnimation.c sets screenPos, node flag
 * 2): position (0, 0, 0.72907) and (0, 0, 0.74207), scale 0.0039999 (their
 * keys; the roll the keys also carry is no part of a camera-held node's
 * matrix, RegistPacket.c reg_setMMatrixPacket), and their type 0 envelopes
 * the node's fade (bga_calcEnvelope), resting at 0.9 and -0.95.
 * reg_dispMObj draws a node with a fade through reg_setDissolve in list 5:
 * 0.9 the sheet added at FIX 9 (ALPHA 0x68), -0.95 subtracted at FIX 4
 * (ALPHA 0x62), with the model's material (the "sandstorm" texture under
 * REPEAT, ATE GEQUAL 96, Z test GEQUAL, Z write).  The dump of the stage
 * (frame v2227) shows exactly that: list 5, two layers of the model's four
 * quadrant packets, GS X 2048 -+ 702.3 and 690.0 (w 3.6454 and 3.7104 =
 * 5 x the keys' depth, the 500 unit screen's scaling of a node nearer
 * than 5).
 *
 * The mechanism: every stage pack carries sandstorm.p2o and sandstorm.tm2
 * (the Film effect's own animations, stageTable rows 67 to 70, place the
 * same model in every stage), so the stage's own sandstorm object is
 * there: its DObj is borrowed for one reg_DispObj with two nodes of the
 * values above in place of its own, then given its own back.  The draws
 * are the game's packets through the game's path (the stage's texture, its
 * material and dissolve packets, the meshes rd_content_extend.c continues
 * to the frame's edges), so rd records them as it records stage 40's.  The
 * 500 unit pair is the full screen's of this tick (ico_photo_view_tick) for
 * the draw, and the display list priority is put back after it.  In stage
 * 40 the stage draws the veil itself: nothing here.  Nothing is allocated
 * or loaded. */
#define VEIL_STAGE 40   /* deja */
#define VEIL_MODEL 1614 /* sandstorm.p2o's model id */
#define VEIL_KIND 35    /* the stage animation objects' kind (objTableScene) */

static const struct {
    float z;    /* the node's depth in the camera's space */
    float fade; /* the dissolve: > 0 added, < 0 subtracted */
} kVeilLayer[2] = {{0.72907043f, 0.9f}, {0.74207032f, -0.95f}};

#define VEIL_SCALE 0.0039999341f

/* the stage's sandstorm object, NULL without one */
static Sub15C *veilObject(void)
{
    GObj *g;

    for (g = isysGObjSearchFromObjKindID_begin(VEIL_KIND); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        Sub15C *d = g->dobj;
        if (d != 0 && d->modelId == VEIL_MODEL && d->model != 0 && d->lightMtx != 0 &&
            d->dispType == 2) {
            return d;
        }
    }
    return 0;
}

void ico_photo_view_veil(void)
{
    static struct DObjNode nodes[2];
    static float mtx[2][16] __attribute__((aligned(16)));
    float saved500[2][16] __attribute__((aligned(16)));
    Sub15C *o;
    int nodeNum;
    ICO_WORD nodeMtx;
    struct DObjNode *own;
    int pri;
    int i;

    static int s_noVeilStage = -1; /* the stage last logged without one */

    if (!v.pair || stage_no == VEIL_STAGE) {
        return;
    }
    if ((o = veilObject()) == 0) {
        if (stage_no != s_noVeilStage) {
            s_noVeilStage = stage_no;
            fprintf(stderr,
                    "photo: Deja vu: this place has no grain sheet; nothing drawn "
                    "(stage %d)\n",
                    stage_no);
        }
        return;
    }
    memset(nodes, 0, sizeof(nodes));
    memset(mtx, 0, sizeof(mtx));
    for (i = 0; i < 2; i++) {
        nodes[i].scale[0] = nodes[i].scale[1] = nodes[i].scale[2] = 1.0f;
        nodes[i].fade = kVeilLayer[i].fade;
        nodes[i].alpha = 1.0f;
        nodes[i].flags.ll = 1 | 2; /* the fade, held in the camera's space */
        nodes[i].pos[2] = kVeilLayer[i].z;
        nodes[i].pos[3] = 1.0f;
        mtx[i][0] = mtx[i][5] = mtx[i][10] = VEIL_SCALE;
        mtx[i][15] = 1.0f;
    }
    memcpy(saved500, matrixptr + 0x640, sizeof(saved500));
    memcpy(matrixptr + 0x640, v.screen500, sizeof(saved500));
    nodeNum = o->nodeNum;
    nodeMtx = o->nodeMtx;
    own = o->nodes;
    pri = dl_GetPri();
    o->nodeNum = 2;
    o->nodeMtx = (ICO_WORD)mtx;
    o->nodes = nodes;
    reg_DispObj(o);
    o->nodeNum = nodeNum;
    o->nodeMtx = nodeMtx;
    o->nodes = own;
    dl_SetDLPriority(pri);
    memcpy(matrixptr + 0x640, saved500, sizeof(saved500));
}
#endif
