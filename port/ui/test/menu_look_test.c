/* menu_look_test.c: the menus' Arimo text against the game's menu sheets.
 *
 * Every menu word is Arimo rasterised on the sheets' texel grid and drawn in
 * the sheets' style (menu_font.h, rd.h rd_create_texture_sheet).  This test
 * holds that look to the sheets it imitates: the sheets are read from the
 * player's disc (menu_sheets.c), the item strips are the ones the game draws
 * (ui__menu_strip_raster) run through the shader's CPU reference
 * (port/render/test/sheet_ref.c), and the two pictures are compared.
 *
 *   testSurvey        measures the sheets (the light-ink items of the menu
 *                     text table on the five languages' sheets) and the
 *                     strips drawn the same way, and pins the constants that
 *                     came from it: kSheetInk (menu_font.c: the levels, the
 *                     halo's strength per language), the halo's reach
 *                     ICO_SHEET_RX / RY and its falloff (the strips' halo
 *                     profile against the sheets'), the floor of
 *                     ICO_SHEET_LEVELS and the grain (shader_consts.h).
 *   testCompareItems  every item of ui_menu_text_items in the five languages:
 *                     the reference strip against the sheet's rectangle,
 *                     both composited on black and on mid-grey with MODULATE
 *                     0x80 (the sprite colour of an unlit row), measured with
 *                     the limits below; writes the pictures.
 *
 *   menu_look_test BASE_ELF DISC_IMAGE OUT_DIR
 *
 * Pictures: OUT_DIR/menu_look/<lang>/<row>.png (the item's first texProperty
 * row), the sheet ("before", left) and the strip ("after", right), on grey,
 * the top row at x4 nearest, the bottom at x4 bilinear; and
 * OUT_DIR/menu_look/<lang>/contact.png, every item of the language at x1.
 *
 * ICO_MENU_LOOK_FIT=1 (development only): searches the levels and each
 * language's rim and fill grey (the halo's shape and gains as they are)
 * for the smallest blurred difference over the light-ink items and prints
 * the best, instead of the checks.  The constants it finds are then written
 * into shader_consts.h, sheet_text.hlsli and kSheetInk by hand.
 *
 * ICO_MENU_LOOK_HALOFIT=1 (development only): with the halo's shape fixed
 * (the reach and the linear taps), coordinate descent over the blur's gain
 * (ICO_SHEET_GAIN, on the English items), each other language's rimGain
 * and the levels, for the smallest blurred difference; prints the lines to
 * paste into shader_consts.h, sheet_text.hlsli and kSheetInk, instead of
 * the checks.
 *
 * ICO_MENU_LOOK_DUMP=1 (development only): per item and language, the raw
 * sheet rectangle and the strip at scales 1, 2 and 4, each magnified 4x
 * nearest on grey, stacked, into OUT_DIR/menu_look/dump/<lang>/<row>.png,
 * instead of the checks (for the eye: the grain and the halo at each
 * scale).
 *
 * ICO_MENU_LOOK_GEOFIT=1 (development only): fits each item's em, width,
 * anchor and capital middle per language to its sheet (geoFit) and prints
 * them, instead of the checks; =2 keeps the width.  The menu text table's
 * values came from it.  ICO_MENU_LOOK_BOLD=x,y and ICO_MENU_LOOK_WIDTH=k
 * set the letters' weight and the menus' width for a run (the fit's
 * grids).  ICO_MENU_LOOK_GEOM=1 prints every item's ink box and capitals
 * against the sheet's; ICO_MENU_LOOK_RIM=1 prints each item's halo opacity
 * (the sheet's, the strip's with the full halo and as drawn: what the
 * table's rim values were set from).  The medians per sheet and the limits'
 * measures per language are always printed.
 *
 * Exit 0, 1 on a failure, 77 without the ELF or the disc image (so it runs
 * where the disc is: main's release validation, never in a worktree).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "charFileManager.h" /* texFile */
#include "df_pack.h"
#include "layout_texture.h" /* texProperty */
#include "menu_font.h"
#include "menu_sheets.h"
#include "menu_text.h"
#include "rd_internal.h" /* rd_write_png */
#include "sheet_ref.h"
#include "shader_consts.h"
#include "strings.h"
#include "tables.h"
#include "ui_internal.h"
#include "vfs.h"
#include "host_fs.h"

/* ------------------------------------------------------------- limits */
/* Provisional until the first run on main with the blurred halo (after
   HALOFIT): then each limit is re-measured, the largest measure plus 25 %.
   The limits of testCompareItems, set from the run on main after the
   geometry fit (the sheets' width UI_SHEET_WIDTH, the letters'
   weight, the table's em, anchor, capital middle, width and spacing per
   language fitted to each sheet, the halo per item, 8 levels; 100 items a
   language with ink on the sheet, 500 in all): the largest measure over the
   five languages plus 25 %.  Measured, median / max per language EN FR DE
   IT ES, the larger of the two backgrounds, the figure 1 left out (below);
   the first run on main, before the fit, in brackets.  The ink box: width
   strip / sheet median 1.000 on every language (1.12 .. 1.25), height 1.000
   (0.93 on German; 0.83 .. 0.92), the first line's capital height 0.98 ..
   1.00 (0.82 .. 0.95), its top +0.00 .. +0.10 rows (+0.5 .. +1.0). */
/* blur 7.9/21.2 8.4/27.3 8.3/15.8 8.1/23.5 8.7/29.3 (27.8 .. 31.9 / 56.2);
   the worst Spanish rows 173 and 142 (menu_PAL_02) */
#define T_BLUR 37.0f
/* plain 11.2/26.9 11.9/34.8 11.7/22.8 11.3/30.7 12.2/44.3 (35.7 .. 41.3 /
   73.6) */
#define T_PLAIN 56.0f
/* the ink box's worst edge, texels: 1/3 1/3 1/3 1/11 1/4 (6 .. 10 / 50); the
   worst Italian row 220 (Vuoi salvare?: the sheet's words are set looser,
   the strip's ink ends 11 texels short) */
#define T_EDGE 14.0f
/* the same for an item of several lines: 1/1 everywhere (15 .. 20 / 40); a
   texel's step */
#define T_EDGE_MULTI 1.5f
/* the first line's capital top and baseline, rows: 0.28/1.29 0.26/1.30
   0.16/1.60 0.16/1.15 0.31/2.33 (0.92 .. 1.00 / 2.33); the worst Spanish
   rows 142 and 173 (menu_PAL_02: the baseline under a descender-heavy
   word) */
#define T_LINE 2.9f
/* the ink amount, strip / sheet: median 1.01 1.04 0.94 1.03 1.02, min 0.82,
   max 1.21 (0.76 .. 0.84, 0.23 .. 1.10) */
#define T_AMOUNT_LO 0.65f
#define T_AMOUNT_HI 1.5f
/* the rim / fill / edge shares, the worst of the three, absolute: median
   0.13 0.07 0.08 0.08 0.07, max 0.53 (0.07 .. 0.21, 0.80): the rimless
   sheets' antialiasing is grey at full opacity where the strips' is white
   at partial opacity, so their softest edge texels count as rim */
#define T_SHARE 0.66f
/* The figure 1 (rows 52, 61 "10", 76, 304; the IT dash of row 414 is now
   inside the common limits): Arimo's 1 has a foot and a long flag the
   sheets' 1 has not, so its ink and box differ whatever the size; worst
   blur 52.5 and plain 62.0 (Italian row 76), edge 7 (German row 52), ink
   0.64 .. 1.37 */
#define T_ONE_BLUR 66.0f
#define T_ONE_PLAIN 78.0f
#define T_ONE_EDGE 9.0f
#define T_ONE_AMOUNT_LO 0.5f
#define T_ONE_AMOUNT_HI 1.7f
#define MIN_INK_AMOUNT 6.0f /* a sheet rectangle with less ink than this is not compared */

/* The survey's expectations (testSurvey): what the scratch survey measured on
   the PAL sheets, per language EN FR DE IT ES. */
static const float kExpFill[UI_LANG_COUNT] = {254.7f, 253.7f, 253.3f, 254.4f, 253.5f};
static const float kExpRim[UI_LANG_COUNT] = {23.9f, 61.1f, 55.7f, 60.8f, 61.7f};
/* the first six texels' alpha, summed: left and right averaged, above and below averaged */
static const float kExpMassX[UI_LANG_COUNT] = {3.10f, 3.73f, 3.99f, 3.78f, 3.64f};
static const float kExpMassY[UI_LANG_COUNT] = {2.15f, 2.83f, 2.99f, 2.90f, 2.74f};
#define TOL_LEVEL 0.6f /* grey levels */
#define TOL_MASS 0.08f
/* the strips' halo against the sheets', measured alike (testSurvey): the
   mean alpha at each distance across (1..PROF_X) and down (1..PROF_Y), per
   entry, on the English items (one shape for every language) */
#define TOL_FALLOFF 0.08f
#define PROF_X 12
#define PROF_Y 8
/* the strips' halo mass (the first six texels, as kExpMassX / Y) against
   the sheets' per language: what kSheetInk's rimGain is set from.
   Provisional until the first run on main after HALOFIT. */
#define TOL_MODEL_MASS 0.30f
/* the grain: the mean |alpha - its 3 x 3 mean| over the halo texels, strip /
   sheet, per language.  Provisional until the first run on main (the band
   is then the measured ratios plus 25 %). */
#define GRAIN_RATIO_LO 0.75f
#define GRAIN_RATIO_HI 1.33f
#define EXP_STEPS 3 /* the median number of antialiasing steps of a sheet's white */

static int failures;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static const char *const kLang[UI_LANG_COUNT] = {"en", "fr", "de", "it", "es"};

/* ------------------------------------------------------------- the rects */

/* One item on one language's sheet: its rectangle's texels. */
typedef struct Rect {
    int item, row, lang;
    int ink; /* UiMenuTextInk */
    int w, h;
    const char *sheet;     /* the sheet's name */
    uint8_t *grey, *alpha; /* w x h, alpha 0..255 */
} Rect;

static IcoVfs *s_vfs;
static Rect *s_rect[UI_LANG_COUNT];
static int s_nrect[UI_LANG_COUNT];

static int rowOfItem(int item)
{
    for (int r = 0; r < ui_menu_text_row_count; r++) {
        if (ui_menu_text_rows[r].item == item) {
            return ui_menu_text_rows[r].row;
        }
    }
    return -1;
}

static void collect(void)
{
    const int texFiles = ms_tex_file_count();
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        s_rect[lang] = calloc((size_t)ui_menu_text_item_count, sizeof(Rect));
        for (int i = 0; i < ui_menu_text_item_count; i++) {
            const UiMenuTextItem *it = &ui_menu_text_items[i];
            const int row = rowOfItem(i);
            if (row < 0) {
                continue;
            }
            const LtProperty *e = &texProperty[row];
            if (e->texU != it->u || e->texV != it->v || e->texW != it->w || e->texH != it->h ||
                e->texFileNo < 0 || e->texFileNo >= texFiles) {
                continue;
            }
            char name[64];
            ms_sheet_name(texFile[e->texFileNo].path, lang, name, sizeof(name));
            const MsSheet *s = ms_sheet_for(s_vfs, name);
            if (!s || it->u + it->w > s->w || it->v + it->h > s->h) {
                continue;
            }
            Rect *r = &s_rect[lang][s_nrect[lang]++];
            r->item = i;
            r->row = row;
            r->lang = lang;
            r->ink = it->ink;
            r->w = it->w;
            r->h = it->h;
            r->sheet = s->name;
            r->grey = malloc((size_t)r->w * (size_t)r->h);
            r->alpha = malloc((size_t)r->w * (size_t)r->h);
            for (int y = 0; y < r->h; y++) {
                for (int x = 0; x < r->w; x++) {
                    const uint8_t *p =
                        s->rgba + ((size_t)(it->v + y) * (size_t)s->w + (size_t)(it->u + x)) * 4;
                    r->grey[y * r->w + x] = p[0];
                    r->alpha[y * r->w + x] = p[3];
                }
            }
        }
    }
}

/* -------------------------------------------------------------- survey */

typedef struct Survey {
    double fill, rim, massX, massY;
    double profX[PROF_X + 1],
        profY[PROF_X + 1]; /* the mean alpha (0..1) at distance d, [0] unused */
    double grain;          /* the mean |alpha - its 3 x 3 mean| (0..1) over the halo texels */
    int sheets;
    int steps[16]; /* the antialiasing steps of each sheet's white */
} Survey;

/* the strips' pictures of the rectangles (refRect), for the survey of the
   model: [lang][k] as s_rect */
typedef struct Pic {
    uint8_t *grey, *alpha;
} Pic;

static Pic *s_model[UI_LANG_COUNT];

static int refRect(const Rect *r, uint8_t *grey, uint8_t *alpha);

static int cmpInt(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

/* The measures of the survey, over the light-ink rectangles of a language,
   of the sheets (model 0) or of the strips drawn in their place (model 1)
   (the definitions are the scratch survey's):
   fill   alpha-weighted mean grey of the texels with grey >= 240 and alpha >= 0.9
   rim    the same over the texels with alpha >= 0.5 and grey <= 100 that have no
          fill texel (grey >= 200, alpha >= 0.7) within a texel
   mass   outside the letters of a short word (w <= 60, h <= 25), the mean alpha
          at distance 1..6 from the first / last fill texel of each row (X) and the
          topmost / lowest of each column (Y), summed over the six distances;
          the profile is the same mean at each distance, 1..PROF_X across and
          1..PROF_Y down
   grain  over the halo texels (alpha >= 13, grey <= 100, no fill texel within
          a texel), the mean |alpha - the mean alpha of its 3 x 3|: the grain's
          strength
   steps  per sheet, the distinct alphas of its white texels (grey >= 240, alpha
          between 13 and 254) taken by at least 1 % of them, merged when within 16:
          the clusters that are not full (< 0.93) */
static void survey(int lang, int model, Survey *out)
{
    memset(out, 0, sizeof(*out));
    double fN = 0, fD = 0, rN = 0, rD = 0, gN = 0, gD = 0;
    double pm[4][PROF_X + 1][2];
    memset(pm, 0, sizeof(pm));
    const char *names[16];
    static long hist[16][256];
    memset(hist, 0, sizeof(hist));
    for (int k = 0; k < s_nrect[lang]; k++) {
        const Rect *r = &s_rect[lang][k];
        if (r->ink != UI_INK_LIGHT || (model && !s_model[lang][k].grey)) {
            continue;
        }
        const uint8_t *G = model ? s_model[lang][k].grey : r->grey;
        const uint8_t *A = model ? s_model[lang][k].alpha : r->alpha;
        const int w = r->w, h = r->h;
        uint8_t *fill = calloc((size_t)w * (size_t)h, 1);
        int nfill = 0;
        for (int i = 0; i < w * h; i++) {
            const int g = G[i], a = A[i];
            if (g >= 240 && a * 10 >= 9 * 255) {
                fN += (double)g * a;
                fD += a;
            }
            if (g >= 200 && a * 10 >= 7 * 255) {
                fill[i] = 1;
                nfill++;
            }
        }
        if (nfill == 0) {
            free(fill);
            continue;
        }
        int si = 0;
        while (si < out->sheets && names[si] != r->sheet) {
            si++;
        }
        if (si == out->sheets && si < 16) {
            names[out->sheets++] = r->sheet;
        }
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                const int i = y * w + x, g = G[i], a = A[i];
                int near = 0;
                for (int dy = -1; dy <= 1 && !near; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx >= 0 && yy >= 0 && xx < w && yy < h && fill[yy * w + xx]) {
                            near = 1;
                            break;
                        }
                    }
                }
                if (a * 2 >= 255 && g <= 100 && !near) {
                    rN += (double)g * a;
                    rD += a;
                }
                if (a >= 13 && g <= 100 && !near && x > 0 && y > 0 && x < w - 1 && y < h - 1) {
                    int m = 0;
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            m += A[(y + dy) * w + x + dx];
                        }
                    }
                    gN += fabs((double)a - m / 9.0) / 255.0;
                    gD += 1;
                }
                if (si < 16 && g >= 240 && a >= 13 && a < 255) {
                    hist[si][a]++;
                }
            }
        }
        if (r->w <= 60 && r->h <= 25 && nfill >= 5) {
            for (int y = 0; y < h; y++) {
                int x0 = -1, x1 = -1;
                for (int x = 0; x < w; x++) {
                    if (fill[y * w + x]) {
                        x0 = x0 < 0 ? x : x0;
                        x1 = x;
                    }
                }
                if (x0 < 0) {
                    continue;
                }
                for (int d = 1; d <= PROF_X; d++) {
                    if (x0 - d >= 0) {
                        pm[0][d][0] += A[y * w + x0 - d];
                        pm[0][d][1] += 1;
                    }
                    if (x1 + d < w) {
                        pm[1][d][0] += A[y * w + x1 + d];
                        pm[1][d][1] += 1;
                    }
                }
            }
            for (int x = 0; x < w; x++) {
                int y0 = -1, y1 = -1;
                for (int y = 0; y < h; y++) {
                    if (fill[y * w + x]) {
                        y0 = y0 < 0 ? y : y0;
                        y1 = y;
                    }
                }
                if (y0 < 0) {
                    continue;
                }
                for (int d = 1; d <= PROF_Y; d++) {
                    if (y0 - d >= 0) {
                        pm[2][d][0] += A[(y0 - d) * w + x];
                        pm[2][d][1] += 1;
                    }
                    if (y1 + d < h) {
                        pm[3][d][0] += A[(y1 + d) * w + x];
                        pm[3][d][1] += 1;
                    }
                }
            }
        }
        free(fill);
    }
    out->fill = fD > 0 ? fN / fD : 0;
    out->rim = rD > 0 ? rN / rD : 0;
    out->grain = gD > 0 ? gN / gD : 0;
    for (int d = 1; d <= PROF_X; d++) {
        double px[2], py[2];
        for (int k = 0; k < 2; k++) {
            px[k] = pm[k][d][1] > 0 ? pm[k][d][0] / pm[k][d][1] / 255.0 : 0;
            py[k] = pm[2 + k][d][1] > 0 ? pm[2 + k][d][0] / pm[2 + k][d][1] / 255.0 : 0;
        }
        out->profX[d] = (px[0] + px[1]) * 0.5;
        out->profY[d] = d <= PROF_Y ? (py[0] + py[1]) * 0.5 : 0.0;
        if (d <= 6) {
            out->massX += out->profX[d];
            out->massY += out->profY[d];
        }
    }
    for (int si = 0; si < out->sheets; si++) {
        long tot = 0;
        for (int a = 0; a < 256; a++) {
            tot += hist[si][a];
        }
        int last = -1000, steps = 0, clusterMax = 0, open = 0;
        for (int a = 0; a < 256; a++) {
            if (hist[si][a] > 0 && hist[si][a] * 100 >= tot) {
                if (open && a - last <= 16) {
                    clusterMax = a;
                } else {
                    if (open && clusterMax * 100 < 93 * 255) {
                        steps++;
                    }
                    open = 1;
                    clusterMax = a;
                }
                last = a;
            }
        }
        if (open && clusterMax * 100 < 93 * 255) {
            steps++;
        }
        out->steps[si] = steps;
    }
}

static void surveyPrint(const char *what, int l, const Survey *sv)
{
    printf("menu_look: survey %s %s: fill %.1f rim %.1f massX %.2f massY %.2f grain %.4f, %d "
           "sheets, steps",
           what, kLang[l], sv->fill, sv->rim, sv->massX, sv->massY, sv->grain, sv->sheets);
    for (int s = 0; s < sv->sheets; s++) {
        printf(" %d", sv->steps[s]);
    }
    printf("\n");
    printf("menu_look: survey %s %s: falloff across", what, kLang[l]);
    for (int d = 1; d <= PROF_X; d++) {
        printf(" %.2f", sv->profX[d]);
    }
    printf(", down");
    for (int d = 1; d <= PROF_Y; d++) {
        printf(" %.2f", sv->profY[d]);
    }
    printf("\n");
}

static void testSurvey(void)
{
    Survey sv[UI_LANG_COUNT], md[UI_LANG_COUNT];
    int steps[UI_LANG_COUNT * 16], nsteps = 0;
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        /* the strips in the sheets' places */
        s_model[l] = calloc((size_t)(s_nrect[l] > 0 ? s_nrect[l] : 1), sizeof(Pic));
        for (int k = 0; k < s_nrect[l]; k++) {
            const Rect *r = &s_rect[l][k];
            if (r->ink != UI_INK_LIGHT) {
                continue;
            }
            Pic *p = &s_model[l][k];
            p->grey = malloc((size_t)r->w * (size_t)r->h);
            p->alpha = malloc((size_t)r->w * (size_t)r->h);
            if (!p->grey || !p->alpha || refRect(r, p->grey, p->alpha) != 0) {
                free(p->grey);
                free(p->alpha);
                p->grey = p->alpha = NULL;
            }
        }
        survey(l, 0, &sv[l]);
        survey(l, 1, &md[l]);
        const UiSheetInk *ink = ui_menu_sheet_ink(l);
        surveyPrint("sheets", l, &sv[l]);
        surveyPrint("strips", l, &md[l]);
        for (int s = 0; s < sv[l].sheets; s++) {
            steps[nsteps++] = sv[l].steps[s];
        }
        CHECK(sv[l].sheets >= 4, "%s: only %d sheets measured", kLang[l], sv[l].sheets);
        CHECK(fabs(sv[l].fill - kExpFill[l]) <= TOL_LEVEL, "%s: fill %.1f, expected %.1f", kLang[l],
              sv[l].fill, kExpFill[l]);
        CHECK(fabs(sv[l].rim - kExpRim[l]) <= TOL_LEVEL, "%s: rim %.1f, expected %.1f", kLang[l],
              sv[l].rim, kExpRim[l]);
        CHECK(fabs(sv[l].massX - kExpMassX[l]) <= TOL_MASS, "%s: massX %.2f, expected %.2f",
              kLang[l], sv[l].massX, kExpMassX[l]);
        CHECK(fabs(sv[l].massY - kExpMassY[l]) <= TOL_MASS, "%s: massY %.2f, expected %.2f",
              kLang[l], sv[l].massY, kExpMassY[l]);
        /* kSheetInk is the measurement, rounded; the fill is the palette's white */
        CHECK(ink->rimOn == 1, "%s: the light ink has its rim", kLang[l]);
        CHECK(fabs(sv[l].rim - (double)ink->rimLevel) <= 1.0, "%s: kSheetInk rim %d, measured %.1f",
              kLang[l], ink->rimLevel, sv[l].rim);
        CHECK(fabs(sv[l].fill - (double)ink->fillLevel) <= 3.0,
              "%s: kSheetInk fill %d, measured %.1f", kLang[l], ink->fillLevel, sv[l].fill);
        /* the halo's strength per language (kSheetInk rimGain): the strips'
           mass against the sheets' */
        CHECK(fabs(md[l].massX - sv[l].massX) <= TOL_MODEL_MASS &&
                  fabs(md[l].massY - sv[l].massY) <= TOL_MODEL_MASS,
              "%s: the strips' halo mass %.2f x %.2f, the sheets' %.2f x %.2f (rimGain %d)",
              kLang[l], md[l].massX, md[l].massY, sv[l].massX, sv[l].massY, ink->rimGain);
        /* the grain's strength against the sheets' */
        const double gr = sv[l].grain > 0 ? md[l].grain / sv[l].grain : 0;
        printf("menu_look: survey %s: grain strip / sheet %.2f\n", kLang[l], gr);
        CHECK(gr >= GRAIN_RATIO_LO && gr <= GRAIN_RATIO_HI,
              "%s: the strips' grain %.4f, the sheets' %.4f (ratio %.2f)", kLang[l], md[l].grain,
              sv[l].grain, gr);
    }
    /* German's rim is grey like the Romance languages', not the English black */
    CHECK(sv[UI_LANG_DE].rim > 40.0 && sv[UI_LANG_EN].rim < 40.0,
          "the German rim is grey (%.1f) and the English one dark (%.1f)", sv[UI_LANG_DE].rim,
          sv[UI_LANG_EN].rim);
    qsort(steps, (size_t)nsteps, sizeof(int), cmpInt);
    const int median = nsteps ? steps[nsteps / 2] : 0;
    printf("menu_look: survey: median %d steps (halo %d x %d, %d levels, gain %d)\n", median,
           ICO_SHEET_RX, ICO_SHEET_RY, ICO_SHEET_LEVELS, ICO_SHEET_GAIN);
    /* the halo's reach and falloff: the strips' halo alpha at each distance
       against the English sheets', twelve across, eight down */
    CHECK(ICO_SHEET_RX == 10 && ICO_SHEET_RY == 7, "the halo's reach %d x %d, the chosen 10 x 7",
          ICO_SHEET_RX, ICO_SHEET_RY);
    for (int d = 1; d <= PROF_X; d++) {
        CHECK(fabs(md[UI_LANG_EN].profX[d] - sv[UI_LANG_EN].profX[d]) <= TOL_FALLOFF,
              "the strips' halo at %d across %.2f, the English sheets' %.2f", d,
              md[UI_LANG_EN].profX[d], sv[UI_LANG_EN].profX[d]);
    }
    /* the first field line below a letter: the sheets step from the edge
       to about 0.78 there and to 0.44 on the second, a sharper start than
       the strips' linear ramp (ICO_SHEET_KY_*), which gives about 0.6 on the
       first line and the sheets' values from the second on; the ramp is the
       chosen shape, so the first line is pinned to a band of its own */
    CHECK(md[UI_LANG_EN].profY[1] >= 0.50 && md[UI_LANG_EN].profY[1] <= sv[UI_LANG_EN].profY[1],
          "the strips' halo on the first line down %.2f, the English sheets' %.2f",
          md[UI_LANG_EN].profY[1], sv[UI_LANG_EN].profY[1]);
    for (int d = 2; d <= PROF_Y; d++) {
        CHECK(fabs(md[UI_LANG_EN].profY[d] - sv[UI_LANG_EN].profY[d]) <= TOL_FALLOFF,
              "the strips' halo at %d down %.2f, the English sheets' %.2f", d,
              md[UI_LANG_EN].profY[d], sv[UI_LANG_EN].profY[d]);
    }
    CHECK(median == EXP_STEPS, "the sheets' white has %d antialiasing steps, expected %d", median,
          EXP_STEPS);
    /* the levels are not the survey's five but never fewer */
    CHECK(ICO_SHEET_LEVELS >= median + 2, "ICO_SHEET_LEVELS %d, fewer than the survey's %d",
          ICO_SHEET_LEVELS, median + 2);
    /* the strips' margin covers the halo's reach and the bilinear texel */
    CHECK(UI_MENU_RIM_X == ICO_SHEET_RX + 1 && UI_MENU_RIM_Y == ICO_SHEET_RY + 1,
          "menu_font.h's halo margin %d x %d for a halo of %d x %d", UI_MENU_RIM_X, UI_MENU_RIM_Y,
          ICO_SHEET_RX, ICO_SHEET_RY);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int k = 0; k < s_nrect[l]; k++) {
            free(s_model[l][k].grey);
            free(s_model[l][k].alpha);
        }
        free(s_model[l]);
        s_model[l] = NULL;
    }
}

/* ------------------------------------------------------------ the strip */

/* the style and the sprite colour (GS, 0x80 = 1) an ink draws with */
static void inkOf(int ink, int rim, int lang, RdSheetStyle *st, int *col)
{
    const UiSheetInk *k = ui_menu_item_ink(ink, rim, lang);
    st->rimOn = k->rimOn;
    st->rimLevel = k->rimLevel;
    st->fillLevel = k->fillLevel;
    st->dither = k->dither;
    st->rimWeight = k->rimWeight;
    st->scale = 1;
    st->rimGain = k->rimGain;
    *col = ink == UI_INK_DARK ? 0 : ink == UI_INK_GREY ? (128 * 151 + 127) / 255 : 128;
}

/* the reference picture of an item at scale strip texels a sheet texel
   ((r->w * scale) x (r->h * scale) texels): grey and alpha 0..255 after the
   texture function (MODULATE with the sprite colour); the halo made by
   rd_sheet_rim (rd_tex_test holds it to sheet_ref.c's) and the texel by
   sheetref_texel_from */
static int refRectAt(const Rect *r, const UiMenuTextItem *it, int scale, uint8_t *grey,
                     uint8_t *alpha)
{
    const int w = r->w * scale, h = r->h * scale;
    uint8_t *cov = malloc((size_t)w * (size_t)h), *rim = malloc((size_t)w * (size_t)h);
    if (!cov || !rim || ui__menu_strip_raster(it, r->lang, cov, r->w, r->h, scale) != 0) {
        free(cov);
        free(rim);
        return -1;
    }
    RdSheetStyle st;
    int col;
    inkOf(r->ink, it->rim[r->lang], r->lang, &st, &col);
    st.scale = (uint8_t)scale;
    rd_sheet_rim(cov, (uint32_t)w, (uint32_t)h, (uint32_t)scale, 0, 0, w, h, rim);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint8_t g, a;
            sheetref_texel_from(cov[y * w + x], rim[y * w + x], x, y, &st, &g, &a);
            const int gm = (g * col + 64) / 128;
            grey[y * w + x] = (uint8_t)(gm > 255 ? 255 : gm);
            const int a255 = a * 255 / 128;
            alpha[y * w + x] = (uint8_t)(a255 > 255 ? 255 : a255);
        }
    }
    free(cov);
    free(rim);
    return 0;
}

static int refRectOf(const Rect *r, const UiMenuTextItem *it, uint8_t *grey, uint8_t *alpha)
{
    return refRectAt(r, it, 1, grey, alpha);
}

static int refRect(const Rect *r, uint8_t *grey, uint8_t *alpha)
{
    return refRectOf(r, &ui_menu_text_items[r->item], grey, alpha);
}

/* ------------------------------------------------------------- measures */

typedef struct Measures {
    float blur[2], plain[2];        /* per background (black, grey) */
    float ink;                      /* the ink amount, texels */
    float left, right, top, bottom; /* the ink box, texels (fractional) */
    float capTop, baseline;         /* the first line's, rows */
    int hasLine;                    /* a run of 4 rows or more (a colon's dots have none) */
    float shareRim, shareFill, shareEdge;
    float mx, my, sdx, sdy; /* the fill's centre and spread (standard deviation), texels */
    float rimMass;          /* the dark rim's opacity (texels of the rim class) per fill amount */
    int ok;
} Measures;

static const float kBg[2] = {0.0f, 128.0f};

/* the picture on a flat background, 0..255 */
static void composite(const uint8_t *grey, const uint8_t *alpha, int n, float bg, float *out)
{
    for (int i = 0; i < n; i++) {
        const float a = (float)alpha[i] / 255.0f;
        out[i] = (float)grey[i] * a + bg * (1.0f - a);
    }
}

static void blur3(const float *in, int w, int h, float *out)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float s = 0.0f;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    const int xx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
                    const int yy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
                    s += in[yy * w + xx];
                }
            }
            out[y * w + x] = s / 9.0f;
        }
    }
}

/* how much of a texel is letter, 0..1, from its picture on black: the fill is
   white, the rim and the halo dark grey */
static float fillness(float onBlack)
{
    const float f = (onBlack - 80.0f) / (200.0f - 80.0f);
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

/* the ink's measures of a picture (grey, alpha): ink amount, box, the first
   line's capital top and baseline (rows crossing a fraction of the line's
   peak row amount, linearly between rows), and the class shares */
static void inkMeasures(const uint8_t *grey, const uint8_t *alpha, int w, int h, Measures *m)
{
    float *amt = calloc((size_t)h, sizeof(float));
    float *col = calloc((size_t)w, sizeof(float));
    int rim = 0, fill = 0, edge = 0, any = 0;
    float total = 0.0f, rimA = 0.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int i = y * w + x;
            const float onBlack = (float)grey[i] * (float)alpha[i] / 255.0f;
            const float f = fillness(onBlack);
            amt[y] += f;
            col[x] += f;
            total += f;
            if (alpha[i] >= 13) {
                any++;
                if (onBlack >= 200.0f) {
                    fill++;
                } else if (onBlack > 80.0f) {
                    edge++;
                } else {
                    rim++;
                    rimA += (float)alpha[i] / 255.0f;
                }
            }
        }
    }
    m->ink = total;
    m->rimMass = total > 0.0f ? rimA / total : 0.0f;
    {
        double sx = 0, sxx = 0, sy = 0, syy = 0;
        for (int x = 0; x < w; x++) {
            sx += col[x] * (x + 0.5);
            sxx += col[x] * (x + 0.5) * (x + 0.5);
        }
        for (int y = 0; y < h; y++) {
            sy += amt[y] * (y + 0.5);
            syy += amt[y] * (y + 0.5) * (y + 0.5);
        }
        const double t = total > 0.0f ? total : 1.0;
        m->mx = (float)(sx / t);
        m->my = (float)(sy / t);
        m->sdx = (float)sqrt(fmax(sxx / t - (sx / t) * (sx / t), 0.0));
        m->sdy = (float)sqrt(fmax(syy / t - (sy / t) * (sy / t), 0.0));
    }
    m->shareRim = any ? (float)rim / (float)any : 0.0f;
    m->shareFill = any ? (float)fill / (float)any : 0.0f;
    m->shareEdge = any ? (float)edge / (float)any : 0.0f;
    /* the box: the first and last row (column) whose amount reaches half a
       texel, edges between texels */
    m->top = m->bottom = m->left = m->right = 0.0f;
    for (int pass = 0; pass < 2; pass++) {
        const float *v = pass ? col : amt;
        const int n = pass ? w : h;
        int a = -1, b = -1;
        for (int i = 0; i < n; i++) {
            if (v[i] >= 0.5f) {
                a = a < 0 ? i : a;
                b = i;
            }
        }
        const float lo = a >= 0 ? (float)a : 0.0f, hi = a >= 0 ? (float)(b + 1) : 0.0f;
        if (pass) {
            m->left = lo;
            m->right = hi;
        } else {
            m->top = lo;
            m->bottom = hi;
        }
    }
    /* the first line: from the first row with ink to the next empty row; a
       run of fewer than 4 rows before it (an accent standing apart, ESPAÑOL's
       tilde) is not the line, unless no run is longer (a colon's dots) */
    int y0 = -1, y1 = -1, f0 = -1, f1 = -1;
    for (int y = 0; y < h; y++) {
        if (amt[y] >= 0.5f) {
            if (y0 < 0) {
                y0 = y;
            }
            y1 = y;
        } else if (y0 >= 0) {
            if (y1 - y0 + 1 >= 4) {
                break;
            }
            if (f0 < 0) {
                f0 = y0;
                f1 = y1;
            }
            y0 = y1 = -1;
        }
    }
    m->hasLine = y0 >= 0;
    if (y0 < 0) {
        y0 = f0;
        y1 = f1;
    }
    m->capTop = m->baseline = 0.0f;
    if (y0 >= 0) {
        float peak = 0.0f;
        for (int y = y0; y <= y1; y++) {
            peak = amt[y] > peak ? amt[y] : peak;
        }
        /* the top: the row amount crosses 15 % of the peak (capitals and
           ascenders alone); the baseline: it falls under 35 % (descenders alone) */
        const float tTop = 0.15f * peak, tBase = 0.35f * peak;
        m->capTop = (float)y0;
        for (int y = y0; y <= y1; y++) {
            if (amt[y] >= tTop) {
                m->capTop = (float)y;
                if (y > y0 && amt[y] > amt[y - 1]) {
                    m->capTop = (float)y - (amt[y] - tTop) / (amt[y] - amt[y - 1]);
                }
                break;
            }
        }
        int yb = y0;
        for (int y = y0; y <= y1; y++) {
            if (amt[y] >= tBase) {
                yb = y;
            }
        }
        m->baseline = (float)yb + 1.0f;
        if (yb < y1 && amt[yb] > amt[yb + 1]) {
            m->baseline = (float)yb + 1.0f - 1.0f + (amt[yb] - tBase) / (amt[yb] - amt[yb + 1]);
        }
    }
    free(amt);
    free(col);
}

static void measure(const Rect *r, const uint8_t *rg, const uint8_t *ra, Measures *sheet,
                    Measures *ref)
{
    const int n = r->w * r->h;
    float *a = malloc((size_t)n * sizeof(float)), *b = malloc((size_t)n * sizeof(float));
    float *ab = malloc((size_t)n * sizeof(float)), *bb = malloc((size_t)n * sizeof(float));
    for (int k = 0; k < 2; k++) {
        composite(r->grey, r->alpha, n, kBg[k], a);
        composite(rg, ra, n, kBg[k], b);
        double d = 0.0, db;
        for (int i = 0; i < n; i++) {
            d += fabsf(a[i] - b[i]);
        }
        ref->plain[k] = (float)(d / n);
        blur3(a, r->w, r->h, ab);
        blur3(b, r->w, r->h, bb);
        db = 0.0;
        for (int i = 0; i < n; i++) {
            db += fabsf(ab[i] - bb[i]);
        }
        ref->blur[k] = (float)(db / n);
    }
    free(a);
    free(b);
    free(ab);
    free(bb);
    inkMeasures(r->grey, r->alpha, r->w, r->h, sheet);
    inkMeasures(rg, ra, r->w, r->h, ref);
}

/* ------------------------------------------------------------ geometry */

/* The ink box and the first line of the strips against the sheets', per
   language and sheet: medians of strip / sheet (the box's width and height,
   the first line's capital height: capital top to baseline), the capital
   top's offset (rows, strip less sheet) and the ink amount.  Single-line
   items with a line on both. ICO_MENU_LOOK_GEOM=1 prints every item. */
#define GEO_SHEETS 16
#define GEO_ITEMS 256

enum { G_W, G_H, G_CAP, G_TOP, G_INK, G_SX, G_SY, G_DX, G_DY, G_N };

typedef struct Geo {
    const char *sheet;
    int n;
    float v[G_N][GEO_ITEMS];
} Geo;

static Geo s_geo[UI_LANG_COUNT][GEO_SHEETS + 1]; /* [GEO_SHEETS]: the language's all */
static float s_geoMed[UI_LANG_COUNT][G_N];       /* the language's medians */

static int cmpFloat(const void *a, const void *b)
{
    const float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y;
}

static float median(const float *v, int n)
{
    float t[GEO_ITEMS];
    if (n <= 0) {
        return 0.0f;
    }
    memcpy(t, v, sizeof(float) * (size_t)n);
    qsort(t, (size_t)n, sizeof(float), cmpFloat);
    return n & 1 ? t[n / 2] : 0.5f * (t[n / 2 - 1] + t[n / 2]);
}

static void geoAdd(const Rect *r, const Measures *ms, const Measures *mr)
{
    const float sw = ms->right - ms->left, sh = ms->bottom - ms->top;
    const float sc = ms->baseline - ms->capTop, rc = mr->baseline - mr->capTop;
    if (!ms->hasLine || !mr->hasLine || sw < 2.0f || sh < 2.0f || sc < 2.0f) {
        return;
    }
    const float v[G_N] = {(mr->right - mr->left) / sw,
                          (mr->bottom - mr->top) / sh,
                          rc / sc,
                          mr->capTop - ms->capTop,
                          mr->ink / ms->ink,
                          mr->sdx / ms->sdx,
                          mr->sdy / ms->sdy,
                          mr->mx - ms->mx,
                          mr->my - ms->my};
    const char *env = getenv("ICO_MENU_LOOK_GEOM");
    if (env && *env == '1') {
        const LtProperty *e = &texProperty[r->row];
        printf(
            "menu_look: geom %s row %d item %d %s tex %dx%d disp %dx%d em %.1f: cap %.2f / "
            "%.2f (x%.3f) width x%.3f height x%.3f top %+.2f ink x%.2f spread x%.3f y%.3f centre %+.2f %+.2f\n",
            kLang[r->lang], r->row, r->item, r->sheet, e->texW, e->texH, e->dispW, e->dispH,
            ui_menu_text_items[r->item].em[r->lang], rc, sc, v[G_CAP], v[G_W], v[G_H], v[G_TOP],
            v[G_INK], v[G_SX], v[G_SY], v[G_DX], v[G_DY]);
    }
    int si = 0;
    while (si < GEO_SHEETS && s_geo[r->lang][si].sheet && s_geo[r->lang][si].sheet != r->sheet) {
        si++;
    }
    Geo *gs[2] = {si < GEO_SHEETS ? &s_geo[r->lang][si] : NULL, &s_geo[r->lang][GEO_SHEETS]};
    for (int k = 0; k < 2; k++) {
        Geo *g = gs[k];
        if (!g || g->n >= GEO_ITEMS) {
            continue;
        }
        if (k == 0) {
            g->sheet = r->sheet;
        }
        for (int m = 0; m < G_N; m++) {
            g->v[m][g->n] = v[m];
        }
        g->n++;
    }
}

static void geoPrint(int lang)
{
    for (int si = 0; si <= GEO_SHEETS; si++) {
        const Geo *g = &s_geo[lang][si];
        if (g->n == 0) {
            continue;
        }
        const char *name = si == GEO_SHEETS ? "all" : g->sheet;
        const char *slash = strrchr(name, '/');
        float med[G_N];
        for (int m = 0; m < G_N; m++) {
            med[m] = median(g->v[m], g->n);
            if (si == GEO_SHEETS) {
                s_geoMed[lang][m] = med[m];
            }
        }
        printf("menu_look: geometry %s %-16s %3d items, strip / sheet: width %.3f height %.3f cap "
               "%.3f top %+.2f ink %.2f spread %.3f %.3f centre %+.2f %+.2f\n",
               kLang[lang], slash ? slash + 1 : name, g->n, med[G_W], med[G_H], med[G_CAP],
               med[G_TOP], med[G_INK], med[G_SX], med[G_SY], med[G_DX], med[G_DY]);
    }
}

/* the measures of every compared item, per language, for the limits'
   comments: blur and plain (the larger background), the worst box edge
   (single and several lines), the worse of capital top and baseline, the
   ink ratio, the worst share */
enum { S_BLUR, S_PLAIN, S_EDGE, S_LINE, S_INK, S_SHARE, S_N };

static float s_stat[UI_LANG_COUNT][S_N][GEO_ITEMS];
static int s_statMulti[UI_LANG_COUNT][GEO_ITEMS];

static void statPrint(int lang, int n)
{
    static const char *const kName[S_N] = {"blur", "plain", "edge", "line", "ink", "share"};
    printf("menu_look: limits %s:", kLang[lang]);
    for (int q = 0; q <= S_N; q++) {
        /* q == S_N: the edge of the items of several lines */
        const int k = q == S_N ? S_EDGE : q;
        float v[GEO_ITEMS], lo = 1e9f, hi = -1e9f;
        int m = 0;
        for (int i = 0; i < n; i++) {
            if (s_statMulti[lang][i] == 2 ||
                (k == S_EDGE && (s_statMulti[lang][i] == 1) != (q == S_N))) {
                continue; /* the figure 1 has its own limits */
            }
            v[m++] = s_stat[lang][k][i];
            lo = s_stat[lang][k][i] < lo ? s_stat[lang][k][i] : lo;
            hi = s_stat[lang][k][i] > hi ? s_stat[lang][k][i] : hi;
        }
        printf(" %s %.2f/%.2f", q == S_N ? "edge(multi)" : kName[q], median(v, m), m ? hi : 0.0f);
        if (k == S_INK) {
            printf(" (min %.2f)", lo);
        }
    }
    printf("\n");
}

/* ------------------------------------------------------------ pictures */

static void writeDirs(const char *dir, int lang)
{
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look", dir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/menu_look/%s", dir, kLang[lang]);
    ico_mkdir(path);
}

/* a w x h picture on grey as RGBA at scale s (nearest or bilinear) into dst
   (pitch dw): the picture's texels are at their centres */
static void blit(uint8_t *dst, int dw, int ox, int oy, const uint8_t *grey, const uint8_t *alpha,
                 int w, int h, int s, int bilinear)
{
    const float bg = 128.0f;
    for (int y = 0; y < h * s; y++) {
        for (int x = 0; x < w * s; x++) {
            float v;
            if (!bilinear) {
                const int i = (y / s) * w + (x / s);
                const float a = (float)alpha[i] / 255.0f;
                v = (float)grey[i] * a + bg * (1.0f - a);
            } else {
                const float fx = ((float)x + 0.5f) / (float)s - 0.5f;
                const float fy = ((float)y + 0.5f) / (float)s - 0.5f;
                const int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
                const float tx = fx - (float)x0, ty = fy - (float)y0;
                float g = 0.0f, a = 0.0f;
                for (int k = 0; k < 4; k++) {
                    const int xx = x0 + (k & 1), yy = y0 + (k >> 1);
                    const float wgt = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? ty : 1.0f - ty);
                    if (xx >= 0 && yy >= 0 && xx < w && yy < h) {
                        const float al = (float)alpha[yy * w + xx] / 255.0f;
                        g += wgt * (float)grey[yy * w + xx] * al;
                        a += wgt * al;
                    }
                }
                v = g + bg * (1.0f - a);
            }
            uint8_t *p = dst + ((size_t)(oy + y) * (size_t)dw + (size_t)(ox + x)) * 4;
            p[0] = p[1] = p[2] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            p[3] = 255;
        }
    }
}

static void writePair(const char *dir, const Rect *r, const uint8_t *rg, const uint8_t *ra)
{
    const int s = 4, gap = 8;
    const int W = 2 * r->w * s + gap, H = 2 * r->h * s + gap;
    uint8_t *img = malloc((size_t)W * (size_t)H * 4);
    if (!img) {
        return;
    }
    for (size_t i = 0; i < (size_t)W * (size_t)H; i++) {
        img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = 40;
        img[i * 4 + 3] = 255;
    }
    blit(img, W, 0, 0, r->grey, r->alpha, r->w, r->h, s, 0);
    blit(img, W, r->w * s + gap, 0, rg, ra, r->w, r->h, s, 0);
    blit(img, W, 0, r->h * s + gap, r->grey, r->alpha, r->w, r->h, s, 1);
    blit(img, W, r->w * s + gap, r->h * s + gap, rg, ra, r->w, r->h, s, 1);
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look/%s/%d.png", dir, kLang[r->lang], r->row);
    rd_write_png(path, img, (uint32_t)W, (uint32_t)H, (uint32_t)W * 4, 1);
    free(img);
}

/* ---------------------------------------------------------- the compare */

static void testCompareItems(const char *outDir)
{
    int total = 0, bad = 0, skipped = 0;
    double worstBlur = 0.0, worstPlain = 0.0;
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        writeDirs(outDir, lang);
        int langBad = 0, langN = 0, contactH = 0, contactW = 0;
        for (int k = 0; k < s_nrect[lang]; k++) {
            contactH += s_rect[lang][k].h + 2;
            contactW = s_rect[lang][k].w > contactW ? s_rect[lang][k].w : contactW;
        }
        const int CW = 2 * contactW + 8;
        uint8_t *contact = calloc((size_t)CW * (size_t)(contactH > 0 ? contactH : 1), 4);
        int cy = 0;
        for (int k = 0; k < s_nrect[lang]; k++) {
            const Rect *r = &s_rect[lang][k];
            const UiMenuTextItem *it = &ui_menu_text_items[r->item];
            uint8_t *rg = malloc((size_t)r->w * (size_t)r->h);
            uint8_t *ra = malloc((size_t)r->w * (size_t)r->h);
            Measures ms, mr;
            if (refRect(r, rg, ra) != 0) {
                CHECK(0, "%s item %d: the strip cannot be rasterised", kLang[lang], r->item);
                free(rg);
                free(ra);
                continue;
            }
            measure(r, rg, ra, &ms, &mr);
            writePair(outDir, r, rg, ra);
            if (contact) {
                blit(contact, CW, 0, cy, r->grey, r->alpha, r->w, r->h, 1, 0);
                blit(contact, CW, contactW + 8, cy, rg, ra, r->w, r->h, 1, 0);
                cy += r->h + 2;
            }
            free(rg);
            free(ra);
            if (ms.ink < MIN_INK_AMOUNT) {
                skipped++;
                continue; /* nothing of the item's words on this sheet */
            }
            total++;
            if (!strchr(ui_str_in((UiLang)lang, (UiStrId)it->str), '\n')) {
                geoAdd(r, &ms, &mr);
            }
            const char *rimEnv = getenv("ICO_MENU_LOOK_RIM");
            if (rimEnv && *rimEnv == '1') {
                /* the halo's opacity per fill: the sheet's, the strip's
                   with the full halo (what the table's rim was set from)
                   and as drawn */
                UiMenuTextItem full = *it;
                full.rim[lang] = UI_RIM_FULL;
                uint8_t *fg = malloc((size_t)r->w * (size_t)r->h);
                uint8_t *fa = malloc((size_t)r->w * (size_t)r->h);
                Measures mf;
                memset(&mf, 0, sizeof(mf));
                if (fg && fa && refRectOf(r, &full, fg, fa) == 0) {
                    inkMeasures(fg, fa, r->w, r->h, &mf);
                }
                free(fg);
                free(fa);
                printf("menu_look: rim %s row %d item %d %s ink %d: sheet %.3f full %.3f (%.2f) "
                       "drawn %.3f (rim %d)\n",
                       kLang[lang], r->row, r->item, r->sheet, r->ink, ms.rimMass, mf.rimMass,
                       mf.rimMass > 0.0f ? ms.rimMass / mf.rimMass : 0.0f, mr.rimMass,
                       it->rim[lang]);
            }
            langN++;
            const int multi = strchr(ui_str_in((UiLang)lang, (UiStrId)it->str), '\n') != NULL;
            /* the figure 1 (and 10): Arimo's 1 has a foot the sheets' has not */
            const int one = it->str == UI_STR_MT_DIGIT_1 || it->str == UI_STR_MT_DIGIT_10;
            if (langN <= GEO_ITEMS) {
                const float b[4] = {fabsf(mr.left - ms.left), fabsf(mr.right - ms.right),
                                    fabsf(mr.top - ms.top), fabsf(mr.bottom - ms.bottom)};
                const float sh[3] = {fabsf(mr.shareRim - ms.shareRim),
                                     fabsf(mr.shareFill - ms.shareFill),
                                     fabsf(mr.shareEdge - ms.shareEdge)};
                const float line =
                    mr.hasLine && ms.hasLine
                        ? fmaxf(fabsf(mr.capTop - ms.capTop), fabsf(mr.baseline - ms.baseline))
                        : 0.0f;
                const float st[S_N] = {fmaxf(mr.blur[0], mr.blur[1]),
                                       fmaxf(mr.plain[0], mr.plain[1]),
                                       fmaxf(fmaxf(b[0], b[1]), fmaxf(b[2], b[3])),
                                       line,
                                       mr.ink / ms.ink,
                                       fmaxf(sh[0], fmaxf(sh[1], sh[2]))};
                for (int q = 0; q < S_N; q++) {
                    s_stat[lang][q][langN - 1] = st[q];
                }
                s_statMulti[lang][langN - 1] = multi ? 1 : one ? 2 : 0;
            }
            const float edge = one ? T_ONE_EDGE : multi ? T_EDGE_MULTI : T_EDGE;
            const float tBlur = one ? T_ONE_BLUR : T_BLUR, tPlain = one ? T_ONE_PLAIN : T_PLAIN;
            const float tLo = one ? T_ONE_AMOUNT_LO : T_AMOUNT_LO,
                        tHi = one ? T_ONE_AMOUNT_HI : T_AMOUNT_HI;
            char why[512];
            why[0] = '\0';
            size_t n = 0;
#define NOTE(...) n += (size_t)snprintf(why + n, sizeof(why) - n, __VA_ARGS__)
            for (int b = 0; b < 2; b++) {
                if (mr.blur[b] > tBlur) {
                    NOTE(" blur[%d] %.1f", b, mr.blur[b]);
                }
                if (mr.plain[b] > tPlain) {
                    NOTE(" plain[%d] %.1f", b, mr.plain[b]);
                }
                worstBlur = mr.blur[b] > worstBlur ? mr.blur[b] : worstBlur;
                worstPlain = mr.plain[b] > worstPlain ? mr.plain[b] : worstPlain;
            }
            const float lr = fabsf(mr.left - ms.left), rr = fabsf(mr.right - ms.right),
                        tr = fabsf(mr.top - ms.top), br = fabsf(mr.bottom - ms.bottom);
            if (lr > edge || rr > edge || tr > edge || br > edge) {
                NOTE(" box dL %.1f dR %.1f dT %.1f dB %.1f", mr.left - ms.left, mr.right - ms.right,
                     mr.top - ms.top, mr.bottom - ms.bottom);
            }
            if (mr.hasLine && ms.hasLine &&
                (fabsf(mr.capTop - ms.capTop) > T_LINE ||
                 fabsf(mr.baseline - ms.baseline) > T_LINE)) {
                NOTE(" cap %.1f base %.1f", mr.capTop - ms.capTop, mr.baseline - ms.baseline);
            }
            const float ratio = mr.ink / ms.ink;
            if (ratio < tLo || ratio > tHi) {
                NOTE(" ink x%.2f", ratio);
            }
            if (fabsf(mr.shareRim - ms.shareRim) > T_SHARE ||
                fabsf(mr.shareFill - ms.shareFill) > T_SHARE ||
                fabsf(mr.shareEdge - ms.shareEdge) > T_SHARE) {
                NOTE(" shares rim %.2f/%.2f fill %.2f/%.2f edge %.2f/%.2f", mr.shareRim,
                     ms.shareRim, mr.shareFill, ms.shareFill, mr.shareEdge, ms.shareEdge);
            }
#undef NOTE
            if (why[0]) {
                bad++;
                langBad++;
                printf("menu_look: %s row %d (item %d, %s):%s\n", kLang[lang], r->row, r->item,
                       r->sheet, why);
            }
        }
        if (contact) {
            char path[1100];
            snprintf(path, sizeof(path), "%s/menu_look/%s/contact.png", outDir, kLang[lang]);
            rd_write_png(path, contact, (uint32_t)CW, (uint32_t)(cy > 0 ? cy : 1), (uint32_t)CW * 4,
                         1);
            free(contact);
        }
        geoPrint(lang);
        statPrint(lang, langN < GEO_ITEMS ? langN : GEO_ITEMS);
        printf("menu_look: %s: %d items compared, %d outside the limits\n", kLang[lang], langN,
               langBad);
        CHECK(langN >= 60, "%s: only %d items compared", kLang[lang], langN);
    }
    printf("menu_look: %d items, %d outside the limits (blur <= %.1f, plain <= %.1f), %d without "
           "ink on the sheet; the worst blur %.1f, plain %.1f\n",
           total, bad, T_BLUR, T_PLAIN, skipped, worstBlur, worstPlain);
    CHECK(bad == 0, "%d items outside the limits", bad);
}

/* -------------------------------------------------------------- geofit */

/* ICO_MENU_LOOK_GEOFIT=1 (development only): per item and language, the
   item's width factor (UiMenuTextItem.wx), the em's factor and the
   anchor's x and y offsets that bring the strip's fill closest to the
   sheet's (the mean difference of the two fill maps after the 3 x 3 blur),
   by coordinate descent; =2 keeps the width.
   Prints each item and the medians per sheet. */

/* the fill map (fillness of the picture on black) blurred */
static void fillMap(const uint8_t *grey, const uint8_t *alpha, int w, int h, float *tmp, float *out)
{
    for (int i = 0; i < w * h; i++) {
        tmp[i] = fillness((float)grey[i] * (float)alpha[i] / 255.0f);
    }
    blur3(tmp, w, h, out);
}

typedef struct GeoFitCtx {
    const Rect *r;
    const float *sheet; /* the sheet's blurred fill map */
    uint8_t *g, *a;
    float *tmp, *map;
} GeoFitCtx;

static double geoErr(GeoFitCtx *c, const float *p)
{
    const Rect *r = c->r;
    UiMenuTextItem t = ui_menu_text_items[r->item];
    t.em[r->lang] *= p[1];
    t.x[r->lang] += p[2];
    t.y[r->lang] += p[3];
    t.track[r->lang] += p[4];
    t.wx[r->lang] *= p[0];
    if (refRectOf(r, &t, c->g, c->a) != 0) {
        return 1e9;
    }
    fillMap(c->g, c->a, r->w, r->h, c->tmp, c->map);
    double d = 0.0;
    for (int i = 0; i < r->w * r->h; i++) {
        d += fabsf(c->map[i] - c->sheet[i]);
    }
    return d / (r->w * r->h) * 255.0;
}

static int cmpD(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static double medianD(double *v, int n)
{
    if (n <= 0) {
        return 0.0;
    }
    qsort(v, (size_t)n, sizeof(double), cmpD);
    return n & 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

static void geoFit(int fixedWidth)
{
    const float w0 = ui__sheet_set_width(UI_SHEET_WIDTH);
    ui__sheet_set_width(w0);
    double tot0 = 0.0, tot = 0.0;
    int totN = 0;

    enum { NP = 5 };

    static double res[UI_LANG_COUNT][512][NP + 2];
    static int has[UI_LANG_COUNT][512];
    memset(has, 0, sizeof(has));
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        for (int k = 0; k < s_nrect[lang]; k++) {
            const Rect *r = &s_rect[lang][k];
            const int n = r->w * r->h;
            GeoFitCtx c;
            float *sheet = malloc(sizeof(float) * (size_t)n);
            c.r = r;
            c.sheet = sheet;
            c.g = malloc((size_t)n);
            c.a = malloc((size_t)n);
            c.tmp = malloc(sizeof(float) * (size_t)n);
            c.map = malloc(sizeof(float) * (size_t)n);
            fillMap(r->grey, r->alpha, r->w, r->h, c.tmp, sheet);
            double ink = 0.0;
            for (int i = 0; i < n; i++) {
                ink += c.tmp[i];
            }
            if (ink >= MIN_INK_AMOUNT) {
                /* several starts (the width, the spacing and the anchor):
                   the descent alone stops in a valley on a long word */
                static const float kStart[6][3] = {{1.0f, 0.0f, 0.0f},  {1.1f, 0.0f, 0.0f},
                                                   {1.0f, 0.75f, 0.0f}, {0.95f, 1.5f, 0.0f},
                                                   {1.0f, 0.0f, -4.0f}, {1.0f, 0.0f, 4.0f}};
                float best[NP];
                double bestE = 1e30;
                const float p0[NP] = {1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                const double e0 = geoErr(&c, p0);
                for (int s0 = 0; s0 < 6; s0++) {
                    float p[NP] = {kStart[s0][0], 1.0f, kStart[s0][2], 0.0f, kStart[s0][1]};
                    float st[NP] = {0.04f, 0.04f, 1.0f, 1.0f, 0.25f};
                    const float minSt[NP] = {0.005f, 0.005f, 0.125f, 0.125f, 0.03f};
                    double e = geoErr(&c, p);
                    for (int it = 0; it < 200; it++) {
                        int better = 0;
                        for (int q = fixedWidth ? 1 : 0; q < NP; q++) {
                            for (int sgn = -1; sgn <= 1; sgn += 2) {
                                float t[NP];
                                memcpy(t, p, sizeof(t));
                                t[q] += (float)sgn * st[q];
                                const double et = geoErr(&c, t);
                                if (et < e - 1e-6) {
                                    e = et;
                                    memcpy(p, t, sizeof(t));
                                    better = 1;
                                }
                            }
                        }
                        /* the em or the spacing up and the width down
                           together (and back): the glyphs keep their width */
                        for (int k = 0; k < 4; k++) {
                            const int sgn = k & 1 ? 1 : -1, q = k < 2 ? 1 : 4;
                            float t[NP];
                            memcpy(t, p, sizeof(t));
                            t[q] += (float)sgn * st[q];
                            t[0] -= (float)sgn * (q == 1 ? st[1] : st[4] * 0.1f);
                            if (fixedWidth) {
                                break;
                            }
                            const double et = geoErr(&c, t);
                            if (et < e - 1e-6) {
                                e = et;
                                memcpy(p, t, sizeof(t));
                                better = 1;
                            }
                        }
                        if (!better) {
                            int more = 0;
                            for (int q = 0; q < NP; q++) {
                                if (st[q] > minSt[q]) {
                                    st[q] *= 0.5f;
                                    more = 1;
                                }
                            }
                            if (!more) {
                                break;
                            }
                        }
                    }
                    if (e < bestE) {
                        bestE = e;
                        memcpy(best, p, sizeof(best));
                    }
                }
                float p[NP];
                memcpy(p, best, sizeof(p));
                const double e = bestE;
                printf("menu_look: geofit %s row %d item %d %s: width %.3f em x%.3f dx %+.2f dy "
                       "%+.2f track %+.2f err %.2f -> %.2f\n",
                       kLang[lang], r->row, r->item, r->sheet, p[0], p[1], p[2], p[3], p[4], e0, e);
                for (int q = 0; q < NP; q++) {
                    res[lang][r->item][q] = p[q];
                }
                tot0 += e0;
                tot += e;
                totN++;
                res[lang][r->item][NP] = e0;
                res[lang][r->item][NP + 1] = e;
                has[lang][r->item] = 1;
            }
            free(sheet);
            free(c.g);
            free(c.a);
            free(c.tmp);
            free(c.map);
        }
    }
    /* per item over the languages: the medians (the table's em and x are
       per item, y per language) */
    for (int i = 0; i < ui_menu_text_item_count; i++) {
        double v[NP + 2][UI_LANG_COUNT];
        int m = 0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            if (has[l][i]) {
                for (int q = 0; q < NP + 2; q++) {
                    v[q][m] = res[l][i][q];
                }
                m++;
            }
        }
        if (!m) {
            continue;
        }
        printf("menu_look: geofit item %d row %d: width %.3f em x%.3f (%.2f) dx %+.2f err %.2f -> "
               "%.2f, dy",
               i, rowOfItem(i), medianD(v[0], m), medianD(v[1], m),
               ui_menu_text_items[i].em[0] * medianD(v[1], m), medianD(v[2], m), medianD(v[NP], m),
               medianD(v[NP + 1], m));
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            if (has[l][i]) {
                printf(" %s %+.2f", kLang[l], res[l][i][3]);
            }
        }
        printf("\n");
    }
    /* per sheet: the medians over its items and languages */
    for (int pass = 0; pass < 16; pass++) {
        const char *name = NULL;
        double v[NP + 2][UI_LANG_COUNT * 512];
        int m = 0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            for (int k = 0; k < s_nrect[l]; k++) {
                const Rect *r = &s_rect[l][k];
                const char *base = strrchr(r->sheet, '/');
                base = base ? base + 1 : r->sheet;
                static const char *seen[16];
                int si = 0;
                while (si < 16 && seen[si] && strcmp(seen[si], base) != 0) {
                    si++;
                }
                if (si < 16 && !seen[si]) {
                    seen[si] = base;
                }
                if (si != pass || !has[l][r->item]) {
                    continue;
                }
                name = base;
                for (int q = 0; q < NP + 2; q++) {
                    v[q][m] = res[l][r->item][q];
                }
                m++;
            }
        }
        if (name) {
            printf("menu_look: geofit sheet %-16s %3d: width %.3f em x%.3f dx %+.2f dy %+.2f err "
                   "%.2f -> %.2f\n",
                   name, m, medianD(v[0], m), medianD(v[1], m), medianD(v[2], m), medianD(v[3], m),
                   medianD(v[NP], m), medianD(v[NP + 1], m));
        }
    }
    printf("menu_look: geofit total %d: err %.3f -> %.3f\n", totN, totN ? tot0 / totN : 0.0,
           totN ? tot / totN : 0.0);
    ui__sheet_set_width(w0);
}

/* ------------------------------------------------------------------ fit */

/* font_sheet_ps's arithmetic (sheet_ref.c) with the constants as arguments:
   v quantised to levels against th (512ths of a step) */
static unsigned quantiseN(unsigned v, unsigned th512, unsigned levels)
{
    const unsigned n = levels - 1;
    unsigned q = (v * n * 512u + th512 * 255u) / (255u * 512u);
    q = q > n ? n : q;
    return (q * 255u + n / 2u) / n;
}

/* the dithered threshold of texel (x, y): the noise tile's entry spread
   ICO_SHEET_GRAIN / 64 of a step round the half */
static unsigned thresholdN(int x, int y)
{
    const unsigned r = sheetref_noise(x, y);
    return ((2u * r + 1u) * ICO_SHEET_GRAIN + 256u * (64u - ICO_SHEET_GRAIN) + 32u) / 64u;
}

typedef struct FitItem {
    const Rect *r;
    uint8_t *cov;  /* the strip */
    uint64_t *sum; /* its halo's weighted sums (the shape fixed: sheetref_halo_sum) */
    uint64_t den;  /* the taps' total */
} FitItem;

static int fitItemMake(FitItem *f, const Rect *r, int lang)
{
    const int n = r->w * r->h;
    f->r = r;
    f->cov = malloc((size_t)n);
    f->sum = malloc(sizeof(uint64_t) * (size_t)n);
    if (!f->cov || !f->sum ||
        ui__menu_strip_raster(&ui_menu_text_items[r->item], lang, f->cov, r->w, r->h, 1) != 0) {
        free(f->cov);
        free(f->sum);
        return 0;
    }
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            f->sum[y * r->w + x] =
                sheetref_halo_sum(f->cov, (uint32_t)r->w, (uint32_t)r->h, x, y, 1, &f->den);
        }
    }
    return 1;
}

static void fitItemFree(FitItem *f)
{
    free(f->cov);
    free(f->sum);
}

/* the item's texels (grey, alpha 0..255) for a style: the halo's gain and
   the weight (the rim weight times the language gain, 64ths), the levels,
   the rim and fill greys */
static void fitTexels(const FitItem *f, unsigned levels, unsigned rim, unsigned fill, unsigned gain,
                      unsigned weight, uint8_t *g, uint8_t *a)
{
    const int w = f->r->w, h = f->r->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const unsigned c = f->cov[y * w + x];
            const uint64_t rr = (f->sum[y * w + x] * gain + f->den * 32u) / (f->den * 64u);
            const unsigned r = rr > 255u ? 255u : (unsigned)rr;
            const unsigned hw = (r * weight + 32u) / 64u;
            const unsigned rw = hw > 255u ? 255u : hw;
            const unsigned av = c > rw ? c : rw;
            const unsigned t = av ? (c * 255u + av / 2u) / av : 0u;
            const unsigned th = thresholdN(x, y);
            const unsigned aq = quantiseN(av, th, levels), tq = quantiseN(t, th, levels);
            g[y * w + x] = (uint8_t)((rim * (255u - tq) + fill * tq + 127u) / 255u);
            const unsigned a128 = (aq * 128u + 127u) / 255u;
            a[y * w + x] = (uint8_t)(a128 * 255u / 128u > 255u ? 255u : a128 * 255u / 128u);
        }
    }
}

/* the blurred difference of one item for a style, over both backgrounds */
static double fitScore(const FitItem *f, unsigned levels, unsigned rim, unsigned fill,
                       unsigned gain, unsigned weight)
{
    const int w = f->r->w, h = f->r->h, n = w * h;
    uint8_t *g = malloc((size_t)n), *a = malloc((size_t)n);
    float *p = malloc((size_t)n * sizeof(float)), *q = malloc((size_t)n * sizeof(float)),
          *pb = malloc((size_t)n * sizeof(float)), *ref = malloc((size_t)n * sizeof(float));
    fitTexels(f, levels, rim, fill, gain, weight, g, a);
    double sum = 0.0;
    for (int k = 0; k < 2; k++) {
        composite(f->r->grey, f->r->alpha, n, kBg[k], ref);
        composite(g, a, n, kBg[k], p);
        blur3(ref, w, h, q);
        blur3(p, w, h, pb);
        for (int i = 0; i < n; i++) {
            sum += fabsf(q[i] - pb[i]);
        }
    }
    free(g);
    free(a);
    free(p);
    free(q);
    free(pb);
    free(ref);
    return sum / (2.0 * n);
}

enum { MAXI = 160 };

/* the light-ink items of a language with their sums; returns the count */
static int fitItems(int lang, FitItem *fi)
{
    int n = 0;
    for (int k = 0; k < s_nrect[lang] && n < MAXI; k++) {
        const Rect *r = &s_rect[lang][k];
        if (r->ink == UI_INK_LIGHT && fitItemMake(&fi[n], r, lang)) {
            n++;
        }
    }
    return n;
}

/* the mean score over a language's items */
static double fitMean(const FitItem *fi, int n, unsigned levels, unsigned rim, unsigned fill,
                      unsigned gain, unsigned weight)
{
    double s = 0.0;
    for (int i = 0; i < n; i++) {
        s += fitScore(&fi[i], levels, rim, fill, gain, weight);
    }
    return s / (n ? n : 1);
}

static unsigned inkWeight(const UiSheetInk *k)
{
    return ((unsigned)(k->rimGain ? k->rimGain : 64u) * 64u + 32u) / 64u;
}

static void fit(void)
{
    /* the grid holds every language's current rim (24 56 61 62) and room
       past it */
    static const unsigned kRims[] = {0, 16, 24, 32, 48, 56, 61, 62, 72, 90, 110, 130, 160};
    static const unsigned kFills[] = {205, 215, 225, 235, 245, 255};
    static FitItem fi[MAXI];
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        const int n = fitItems(lang, fi);
        double best = 1e9, atCurrent = 1e9;
        unsigned bl = 0, br = 0, bf = 0;
        const UiSheetInk *cur = ui_menu_sheet_ink(lang);
        const unsigned wt = inkWeight(cur);
        for (unsigned lv = 3; lv <= 9; lv++) {
            for (size_t ri = 0; ri < sizeof(kRims) / sizeof(kRims[0]); ri++) {
                for (size_t fl = 0; fl < sizeof(kFills) / sizeof(kFills[0]); fl++) {
                    const double s = fitMean(fi, n, lv, kRims[ri], kFills[fl], ICO_SHEET_GAIN, wt);
                    if (s < best) {
                        best = s;
                        bl = lv;
                        br = kRims[ri];
                        bf = kFills[fl];
                    }
                    if (lv == (unsigned)ICO_SHEET_LEVELS && kRims[ri] == cur->rimLevel &&
                        kFills[fl] == cur->fillLevel) {
                        atCurrent = s;
                    }
                }
            }
        }
        printf("menu_look: fit %s (%d items): best blur %.2f at LEVELS %u rim %u fill %u;"
               " the constants (LEVELS %d rim %d fill %d): %.2f\n",
               kLang[lang], n, best, bl, br, bf, ICO_SHEET_LEVELS, cur->rimLevel, cur->fillLevel,
               atCurrent);
        for (int i = 0; i < n; i++) {
            fitItemFree(&fi[i]);
        }
    }
}

/* one parameter by coordinate descent: steps from 16 halving to 1 within
   lo..hi, the score's smallest; *v holds the start and the result */
typedef double (*HaloScore)(unsigned v, void *ctx);

static double descend(unsigned *v, unsigned lo, unsigned hi, HaloScore fn, void *ctx)
{
    double e = fn(*v, ctx);
    for (unsigned step = 16; step >= 1; step /= 2) {
        for (int better = 1; better;) {
            better = 0;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                const int t = (int)*v + sgn * (int)step;
                if (t < (int)lo || t > (int)hi) {
                    continue;
                }
                const double et = fn((unsigned)t, ctx);
                if (et < e - 1e-9) {
                    e = et;
                    *v = (unsigned)t;
                    better = 1;
                }
            }
        }
    }
    return e;
}

typedef struct HaloCtx {
    const FitItem *fi;
    int n;
    unsigned levels, rim, fill, gain, weight;
} HaloCtx;

static double haloByGain(unsigned v, void *p)
{
    const HaloCtx *c = p;
    return fitMean(c->fi, c->n, c->levels, c->rim, c->fill, v, c->weight);
}

static double haloByWeight(unsigned v, void *p)
{
    const HaloCtx *c = p;
    return fitMean(c->fi, c->n, c->levels, c->rim, c->fill, c->gain, v);
}

/* ICO_MENU_LOOK_HALOFIT=1: for each level count, the blur's gain on the
   English items (rimGain 64), then each other language's rimGain; the
   level count with the smallest mean over the languages wins */
static void haloFit(void)
{
    static FitItem fi[UI_LANG_COUNT][MAXI];
    int n[UI_LANG_COUNT];
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        n[l] = fitItems(l, fi[l]);
    }
    double bestTotal = 1e30;
    unsigned bestLv = 0, bestGain = 0, bestW[UI_LANG_COUNT] = {0};
    for (unsigned lv = 4; lv <= 8; lv++) {
        unsigned gain = ICO_SHEET_GAIN, w[UI_LANG_COUNT];
        double total = 0.0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            const UiSheetInk *k = ui_menu_sheet_ink(l);
            HaloCtx c = {fi[l], n[l], lv, k->rimLevel, k->fillLevel, gain, 64};
            w[l] = 64;
            if (l == UI_LANG_EN) {
                total += descend(&gain, 16, 1023, haloByGain, &c);
            } else {
                w[l] = inkWeight(k);
                total += descend(&w[l], 16, 255, haloByWeight, &c);
            }
        }
        printf("menu_look: halofit LEVELS %u: gain %u, rimGain EN %u FR %u DE %u IT %u ES %u, "
               "mean blur %.3f\n",
               lv, gain, w[0], w[1], w[2], w[3], w[4], total / UI_LANG_COUNT);
        if (total < bestTotal) {
            bestTotal = total;
            bestLv = lv;
            bestGain = gain;
            memcpy(bestW, w, sizeof(w));
        }
    }
    {
        double cur = 0.0;
        for (int l = 0; l < UI_LANG_COUNT; l++) {
            const UiSheetInk *k = ui_menu_sheet_ink(l);
            cur += fitMean(fi[l], n[l], ICO_SHEET_LEVELS, k->rimLevel, k->fillLevel, ICO_SHEET_GAIN,
                           inkWeight(k));
        }
        printf("menu_look: halofit the constants in force (LEVELS %d, gain %d): mean blur %.3f\n",
               ICO_SHEET_LEVELS, ICO_SHEET_GAIN, cur / UI_LANG_COUNT);
    }
    printf("menu_look: halofit best: mean blur %.3f; paste:\n", bestTotal / UI_LANG_COUNT);
    printf("  shader_consts.h:   #define ICO_SHEET_GAIN %u\n", bestGain);
    printf("  shader_consts.h:   #define ICO_SHEET_LEVELS %u\n", bestLv);
    printf("  sheet_text.hlsli:  #define SHEET_LEVELS %u\n", bestLv);
    printf("  menu_font.c kSheetInk rimGain: EN %u FR %u DE %u IT %u ES %u\n", bestW[0], bestW[1],
           bestW[2], bestW[3], bestW[4]);
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int i = 0; i < n[l]; i++) {
            fitItemFree(&fi[l][i]);
        }
    }
}

/* The reference of this file's own arithmetic against sheet_ref.c's, for the
   constants in force (the fit modes' copy is only worth its numbers when it
   agrees). */
static void testFitCopy(void)
{
    const Rect *r = NULL;
    for (int k = 0; k < s_nrect[UI_LANG_EN] && !r; k++) {
        if (s_rect[UI_LANG_EN][k].ink == UI_INK_LIGHT) {
            r = &s_rect[UI_LANG_EN][k];
        }
    }
    if (!r) {
        return;
    }
    FitItem f;
    CHECK(fitItemMake(&f, r, UI_LANG_EN), "the strip of the first item");
    const UiSheetInk *k = ui_menu_sheet_ink(UI_LANG_EN);
    RdSheetStyle st = {k->rimOn, k->rimLevel, k->fillLevel, k->dither, k->rimWeight, 1, k->rimGain};
    const int n = r->w * r->h;
    uint8_t *g = malloc((size_t)n), *a = malloc((size_t)n);
    fitTexels(&f, ICO_SHEET_LEVELS, k->rimLevel, k->fillLevel, ICO_SHEET_GAIN, sheetref_weight(&st),
              g, a);
    int diff = 0;
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            uint8_t rg, ra;
            sheetref_texel(f.cov, (uint32_t)r->w, (uint32_t)r->h, x, y, &st, &rg, &ra);
            const unsigned a255 = ra * 255u / 128u > 255u ? 255u : ra * 255u / 128u;
            diff += g[y * r->w + x] != rg || a[y * r->w + x] != a255;
        }
    }
    CHECK(diff == 0,
          "the fit modes' copy of the sheet arithmetic differs from sheet_ref.c at %d texels",
          diff);
    free(g);
    free(a);
    fitItemFree(&f);
}

/* ---------------------------------------------------------------- dump */

/* ICO_MENU_LOOK_DUMP=1: per item and language, the sheet's rectangle and
   the strip at scales 1, 2 and 4, each 4x nearest on grey, stacked */
static void dumpRows(const char *outDir)
{
    static const int kScales[3] = {1, 2, 4};
    char path[1100];
    snprintf(path, sizeof(path), "%s/menu_look", outDir);
    ico_mkdir(path);
    snprintf(path, sizeof(path), "%s/menu_look/dump", outDir);
    ico_mkdir(path);
    int written = 0;
    for (int lang = 0; lang < UI_LANG_COUNT; lang++) {
        snprintf(path, sizeof(path), "%s/menu_look/dump/%s", outDir, kLang[lang]);
        ico_mkdir(path);
        for (int k = 0; k < s_nrect[lang]; k++) {
            const Rect *r = &s_rect[lang][k];
            const int m = 4, gap = 8;
            const int W = r->w * 4 * m, H = r->h * m * (1 + 1 + 2 + 4) + 4 * gap;
            uint8_t *img = malloc((size_t)W * (size_t)H * 4);
            if (!img) {
                continue;
            }
            for (size_t i = 0; i < (size_t)W * (size_t)H; i++) {
                img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = 40;
                img[i * 4 + 3] = 255;
            }
            blit(img, W, 0, 0, r->grey, r->alpha, r->w, r->h, m, 0);
            int y = r->h * m + gap;
            for (int si = 0; si < 3; si++) {
                const int s = kScales[si];
                uint8_t *g = malloc((size_t)(r->w * s) * (size_t)(r->h * s));
                uint8_t *a = malloc((size_t)(r->w * s) * (size_t)(r->h * s));
                if (g && a && refRectAt(r, &ui_menu_text_items[r->item], s, g, a) == 0) {
                    blit(img, W, 0, y, g, a, r->w * s, r->h * s, m, 0);
                }
                free(g);
                free(a);
                y += r->h * s * m + gap;
            }
            snprintf(path, sizeof(path), "%s/menu_look/dump/%s/%d.png", outDir, kLang[lang],
                     r->row);
            rd_write_png(path, img, (uint32_t)W, (uint32_t)H, (uint32_t)W * 4, 1);
            free(img);
            written++;
        }
    }
    printf("menu_look: dump: %d pictures in %s/menu_look/dump\n", written, outDir);
}

/* ----------------------------------------------------------------- main */

static uint8_t *readAll(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *size = buf ? (size_t)n : 0;
    return buf;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: menu_look_test BASE_ELF DISC_IMAGE OUT_DIR\n");
        return 2;
    }
    size_t size;
    uint8_t *elf = readAll(argv[1], &size);
    if (!elf) {
        printf("menu_look: no base ELF (%s): skipped\n", argv[1]);
        return 77;
    }
    char err[512];
    const int rc = ico_tables_load_elf(elf, size, err, sizeof(err));
    free(elf);
    if (rc != 0) {
        printf("menu_look: the ELF's tables do not load: %s\n", err);
        return 1;
    }
    s_vfs = ico_vfs_mount(&ico_vfs_iso9660, argv[2]);
    if (!s_vfs) {
        printf("menu_look: no disc image (%s): skipped\n", argv[2]);
        return 77;
    }
    collect();
    int n = 0;
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        n += s_nrect[l];
    }
    if (n == 0) {
        printf("menu_look: no sheet could be read from the disc image: skipped\n");
        ico_vfs_unmount(s_vfs);
        return 77;
    }
    const char *fitEnv = getenv("ICO_MENU_LOOK_FIT");
    const char *geoEnv = getenv("ICO_MENU_LOOK_GEOFIT");
    const char *boldEnv = getenv("ICO_MENU_LOOK_BOLD");
    if (boldEnv) {
        float bx = 0.0f, by = 0.0f;
        if (sscanf(boldEnv, "%f,%f", &bx, &by) == 2) {
            ui__menu_set_bold(bx, by);
            printf("menu_look: bold %.2f %.2f\n", bx, by);
        }
    }
    const char *widthEnv = getenv("ICO_MENU_LOOK_WIDTH");
    if (widthEnv) {
        ui__sheet_set_width((float)atof(widthEnv));
    }
    const char *haloEnv = getenv("ICO_MENU_LOOK_HALOFIT");
    const char *dumpEnv = getenv("ICO_MENU_LOOK_DUMP");
    if (geoEnv && (*geoEnv == '1' || *geoEnv == '2')) {
        geoFit(*geoEnv == '2');
    } else if (fitEnv && *fitEnv == '1') {
        fit();
    } else if (haloEnv && *haloEnv == '1') {
        haloFit();
    } else if (dumpEnv && *dumpEnv == '1') {
        dumpRows(argv[3]);
    } else {
        testSurvey();
        testFitCopy();
        testCompareItems(argv[3]);
    }
    for (int l = 0; l < UI_LANG_COUNT; l++) {
        for (int k = 0; k < s_nrect[l]; k++) {
            free(s_rect[l][k].grey);
            free(s_rect[l][k].alpha);
        }
        free(s_rect[l]);
    }
    ms_sheets_free();
    ico_vfs_unmount(s_vfs);
    printf("menu_look: %d failures\n", failures);
    return failures ? 1 : 0;
}
