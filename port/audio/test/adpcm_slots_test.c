/*
 * port/audio/test/adpcm_slots_test.c
 *
 * Issue 67: the ADPCM request daemon (scpSubAdpcmPlay, script.c) with both
 * stream slots taken.  The decision it now makes is AdpcmFadingCloseNow
 * (adpcm_init.c): a stream already fading out is closed at once and gives
 * its slot up; only when none is fading does the PS2's path run (the
 * daemon's AdpcmFadeCloseAll).  The game's adpcm_init.c is compiled into
 * this test with the sound driver, the disc reader and the sound-data table
 * stubbed; the log lines are captured from ico_diag_log.
 *
 * The boat scene's case: the sea stream (7) is fading from the skip, the
 * boat's stream (8) is open, and the music (10) is requested.
 *
 * Exit status 0 when every check passes.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "../../../ico2/fumi/sound/adpcm_init.c"

static int failures;
static int checks;

static char logbuf[16][96];
static int nlog;

void ico_diag_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (nlog < 16)
        vsnprintf(logbuf[nlog++], sizeof logbuf[0], fmt, ap);
    va_end(ap);
}

void ico_audio_tag_stream(int slot, int no)
{
    (void)slot;
    (void)no;
}

/* the stubs adpcm_init.c links against */
const AdpcmDataRec adpcmFile[16] = {
    [7] = {.sectors = 100, .pitch = 0x1000, .channels = 2},
    [8] = {.sectors = 100, .pitch = 0x1000, .channels = 2},
    [10] = {.sectors = 100, .pitch = 0x1000, .channels = 2},
};

static SqEntry sqTbl[16];
static int closedCh;

SqEntry *soundDataAreaGet(int no, int bank, int mode, int seg)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (*(int *)&sqTbl[i] == 0) {
            memset(&sqTbl[i], 0, sizeof sqTbl[i]);
            sqTbl[i].num = (unsigned short)no;
            sqTbl[i].bank = (unsigned short)bank;
            sqTbl[i].mode = (unsigned short)mode;
            sqTbl[i].seg = (unsigned short)seg;
            return &sqTbl[i];
        }
    }
    return 0;
}

void soundDataClose(SqEntry *self)
{
    if (self->mode == 2)
        AdpcmClose(self);
    *(int *)self = 0;
}

static int chNext;

int soundBufAdpcmChAlloc(SqEntry *self, int *chp)
{
    (void)self;
    *chp = chNext++ & 3;
    return 0;
}

void soundBufAdpcmFree(SqEntry *self)
{
    (void)self;
}

int Ee2Iop(ICO_WORD ee, int iop, int size)
{
    (void)ee;
    (void)iop;
    (void)size;
    return 0;
}

CdvdBgReq *iosCdvdBackGroundMgrAdd(const char *name, void *readFunc, void *readArg, void *readyFunc,
                                   void *resumeFunc, void *cbArg, void *closeFunc, void *closeArg)
{
    static int dummy[64];
    (void)name;
    (void)readFunc;
    (void)readArg;
    (void)readyFunc;
    (void)resumeFunc;
    (void)cbArg;
    (void)closeFunc;
    (void)closeArg;
    return (CdvdBgReq *)dummy;
}

void iosCdvdBackGroundMgrDelete(CdvdBgReq *self)
{
    (void)self;
}

void iosCdvdBackGroundMgrSeek(CdvdBgReq *self, int val)
{
    (void)self;
    (void)val;
}

int SgStAdpcmOpen(void *req)
{
    (void)req;
    return 0;
}

int SgStAdpcmClose(unsigned int ch)
{
    (void)ch;
    closedCh++;
    return 0;
}

int SgStAdpcmChannelVolume(unsigned long long m, unsigned int l, int r)
{
    (void)m;
    (void)l;
    (void)r;
    return 0;
}

int SgStAdpcmChannelPitch(unsigned long long m, int p)
{
    (void)m;
    (void)p;
    return 0;
}

int SgStAdpcmPlay(unsigned long long m)
{
    (void)m;
    return 0;
}

int SgStAdpcmStop(unsigned long long m)
{
    (void)m;
    return 0;
}

int SgStAdpcmIopReadAddr(int ch)
{
    (void)ch;
    return 0;
}

int iosCdvdDiskStatusGet(void)
{
    return 0;
}

int SgGetSpuSlotMalloc(int mode)
{
    (void)mode;
    return 0;
}

void SgStAdpcmInit(void) {}

int iosCdvdBackGroundReadIOPm(CdvdBgReq *self, void *buf, int size)
{
    (void)self;
    (void)buf;
    (void)size;
    return 0;
}

int iosSifAllocIopHeapDebug(int size, const char *file, int line)
{
    (void)size;
    (void)file;
    (void)line;
    return 0;
}

SqEntry *soundDataAreaSearch(int *pk)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (*(int *)&sqTbl[i] == *pk)
            return &sqTbl[i];
    }
    return 0;
}

int soundOutputModeGet(void)
{
    return 0;
}

int sceSifFreeIopHeap(int a)
{
    (void)a;
    return 0;
}

void __assert(const char *f, int l, const char *e)
{
    printf("FAIL: assert %s:%d %s\n", f, l, e);
    failures++;
}

void debug_assert(const char *f, int l)
{
    (void)f;
    (void)l;
}

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

static void check_eq(long got, long want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s: got %ld, want %ld\n", what, got, want);
    }
}

static void check_log(int i, const char *want)
{
    checks++;
    if (i >= nlog || strcmp(logbuf[i], want) != 0) {
        failures++;
        printf("FAIL: log %d: got \"%s\", want \"%s\"\n", i, i < nlog ? logbuf[i] : "(none)", want);
    }
}

/* the daemon's open of stream `no`: the slot's ring, as AdpcmIopBuffAlloc
   hands it out */
static SqEntry *open_stream(int no)
{
    int buf = AdpcmIopBuffAlloc();
    return adpcmDataSet(0, no, 0x11, 0, 0, buf, 0);
}

static void reset(void)
{
    memset(adpcmStream, 0, sizeof adpcmStream);
    memset(adpcmIopBuffUsed, 0, sizeof adpcmIopBuffUsed);
    memset(sqTbl, 0, sizeof sqTbl);
    memset(adpcmObj, 0, sizeof adpcmObj);
    adpcmIopBase = 0x100000;
    nlog = 0;
    chNext = 0;
}

int main(void)
{
    SqEntry *sea;
    SqEntry *boat;
    SqEntry *music;

    /* A: the sea stream (7) is fading, the boat's (8) is open, 10 is
       requested.  The daemon's loop: no free area, so AdpcmFadingCloseNow
       runs first. */
    reset();
    sea = open_stream(7);
    boat = open_stream(8);
    check_eq(AdpcmFreeAreaGet(), 0, "A both rings taken");
    check_log(0, "adpcm: open 7 slot 0");
    check_log(1, "adpcm: open 8 slot 1");
    AdpcmFadeCloseAll(0x3FFF);
    /* the skip fades only the sea: put the boat back to steady */
    boat->stream->fadeStep = 0;
    nlog = 2;
    check_eq(AdpcmFadingCloseNow(), 1, "A one stream closed now");
    check_eq(AdpcmFreeAreaGet(), 1, "A a ring came free");
    check_eq(sea->num, 0, "A sea entry closed");
    check_eq(boat->num, 8, "A boat entry kept");
    check_eq(boat->stream->used, 1, "A boat stream kept");
    check_log(2, "adpcm: close 7 (now)");
    check_eq(nlog, 3, "A one close line");
    music = open_stream(10);
    check_eq(music->stream->used, 1, "A music opened");
    check_log(3, "adpcm: open 10 slot 0");
    check_eq(adpcmNo[0], 10, "A music took the sea's slot");
    check_eq(adpcmNo[1], 8, "A boat still in its slot");

    /* a stream whose fade runs out is closed "fade" */
    nlog = 0;
    boat->stream->fadeStep = 0x3FFF;
    adpcmTickProc2(boat);
    check_log(0, "adpcm: close 8 (fade)");

    /* B: nothing is fading: no stream is touched and the PS2's path runs
       (the daemon logs the list and fades them all). */
    reset();
    open_stream(7);
    open_stream(8);
    nlog = 2;
    check_eq(AdpcmFadingCloseNow(), 0, "B nothing closed");
    check_eq(AdpcmFreeAreaGet(), 0, "B rings still taken");
    check_eq(adpcmStream[0].used, 1, "B stream 7 kept");
    check_eq(adpcmStream[1].used, 1, "B stream 8 kept");
    check_eq(nlog, 2, "B no log");
    AdpcmLogFull();
    check_log(2, "adpcm: full: closing 7 8");
    AdpcmFadeCloseAll(0x3FFF);
    check_eq(adpcmStream[0].fadeStep, 0x3FFF, "B stream 7 fades");
    check_eq(adpcmStream[1].fadeStep, 0x3FFF, "B stream 8 fades");

    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
