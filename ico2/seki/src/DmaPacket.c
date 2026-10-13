#include "typedef.h"
#include "DmaPacket.h"
#include "ios.h"
#include "memory.h"
#include <stdio.h>

/* the double-buffered packet area every packet builder writes into */
DpkCtl PacketBufferStruct = {0};

/* the DMA memory use debug's meter draws */
int used_dma_memory = 0;

/* PC port: the packet banks' fill.  Every packet builder writes through
   PacketBufferStruct.ptr with no check (only lightning.c asks
   dpk_CheckBufferSize), so a frame that needs more than the EE's 512 KB
   writes past its bank.  A wide picture and the cull against the blended
   pictures' cameras (GsBase.c) record more parts than the PS2 ever did, so
   the host gives each bank DPK_HOST_PAD bytes past the EE's size, where an
   overflow lands harmlessly, and at each frame's end (dpk_SwapBuffer)
   writes the bank's fill to the log (logs/ico-pc.log) once it passes the
   EE's size, and again each time the run's peak grows by another 16 KB.
   The game never reads the size of the allocation, so the pad changes
   nothing else; it comes out of the common partition's host extra
   (ios.c ICO_HOST_COMMON_EXTRA).

   A tick whose camera step is past half the
   presenter's limits registers every box (GsBase.c gsbHostCullViews), the
   whole stage at most.  The largest stage pack (st02a: 87 models, 225
   parts, 862 packets; st20a 303 parts and 773 packets; counted on the PAL
   disc) registered whole is about 862 packets x 0x80 (texture, material
   and microcode packets) plus some 200 objects x 0x180 (the matrix packet
   0xE0, the light packet 0x90, the end tag), under 200 KB.  The pad is
   512 KB, a full stage two and a half times over on top of a full EE bank.
   Three guards keep it from being run past: gsbHostCullViews stops keeping
   every box once the bank is past half of DPK_EE_SIZE (dpk_HostFill), the
   registrations ask dpk_HostRoom before writing an object (RegistPacket.c),
   which refuses, with a log line, once the fill is within DPK_HOST_RESERVE
   of the pad's end, and the pad holds whatever the other builders write
   after that point.  Both banks: 2 x 512 KB more than the EE's, from
   ICO_HOST_COMMON_EXTRA. */
#define DPK_HOST_PAD 0x80000
#define DPK_HOST_RESERVE 0x10000

static long dpkHostPeak; /* port: the largest fill seen, bytes */

static void dpkHostHighWater(void)
{
    const char *base = (const char *)PacketBufferStruct.buf[PacketBufferStruct.cur];
    long used;

    if (base == 0 || PacketBufferStruct.ptr.c == 0) {
        return;
    }
    used = (long)(PacketBufferStruct.ptr.c - base);
    /* a cursor left in some other buffer says nothing about this bank */
    if (used <= DPK_EE_SIZE || used > DPK_EE_SIZE + DPK_HOST_PAD * 16L) {
        return;
    }
    if (used > dpkHostPeak) {
        if (dpkHostPeak <= DPK_EE_SIZE ||
            ((used - DPK_EE_SIZE - 1) >> 14) != ((dpkHostPeak - DPK_EE_SIZE - 1) >> 14)) {
            fprintf(stderr,
                    "dpk: a frame's packets took %ld bytes, past the EE's %d%s (logged per "
                    "16 KB of peak)\n",
                    used, DPK_EE_SIZE,
                    used > DPK_EE_SIZE + DPK_HOST_PAD
                        ? " and past the host's pad: memory after the bank was overwritten"
                        : ", inside the host's pad");
        }
        dpkHostPeak = used;
    }
}

long dpk_HostFill(void)
{
    const char *base = (const char *)PacketBufferStruct.buf[PacketBufferStruct.cur];

    if (base == 0 || PacketBufferStruct.ptr.c == 0) {
        return 0;
    }
    return (long)(PacketBufferStruct.ptr.c - base);
}

/* the frames that refused an object, for the log (one line per 600 of
   them) */
static unsigned int dpkHostRefusedFrames; /* port */

static int dpkHostRefusedNow; /* port: this frame refused one */

int dpk_HostRoom(void)
{
    const long fill = dpk_HostFill();

    if (fill <= (long)(DPK_EE_SIZE + DPK_HOST_PAD - DPK_HOST_RESERVE)) {
        return 1;
    }
    if (!dpkHostRefusedNow) {
        dpkHostRefusedNow = 1;
        if (dpkHostRefusedFrames++ % 600 == 0) {
            fprintf(stderr,
                    "dpk: a frame's packets reached %ld bytes, within %d of the host's pad's end "
                    "(%d): its remaining objects are not registered (frame %u of those; logged "
                    "per 600)\n",
                    fill, DPK_HOST_RESERVE, DPK_EE_SIZE + DPK_HOST_PAD, dpkHostRefusedFrames);
        }
    }
    return 0;
}

void dpk_Init(void)
{
    PacketBufferStruct.cur = 0;
    PacketBufferStruct.buf[0] = (int *)ICO_UNCACHED_ACCEL(ICO_ADDR(
        iosMallocDebug(ios_partition_common, DPK_EE_SIZE + DPK_HOST_PAD, "src/DmaPacket.c", 134)));
    PacketBufferStruct.buf[1] = (int *)ICO_UNCACHED_ACCEL(ICO_ADDR(
        iosMallocDebug(ios_partition_common, DPK_EE_SIZE + DPK_HOST_PAD, "src/DmaPacket.c", 135)));
    PacketBufferStruct.ptr.i = PacketBufferStruct.buf[PacketBufferStruct.cur];
}

void dpk_SwapBuffer(void)
{
    int i;

    dpkHostHighWater();
    dpkHostRefusedNow = 0;
    i = PacketBufferStruct.cur ^ 1;
    PacketBufferStruct.cur = i;
    PacketBufferStruct.ptr.i = PacketBufferStruct.buf[i];
    PacketBufferStruct.tail.c = 0;
    PacketBufferStruct.gif.c = 0;
    PacketBufferStruct.end.c = 0;
}

unsigned int dpk_CheckBufferSize(void)
{
    int idx = PacketBufferStruct.cur;
    ICO_WORD adj_cur = (ICO_WORD)PacketBufferStruct.ptr.c - 0x80000;
    ICO_WORD end_off = (ICO_WORD)PacketBufferStruct.buf[idx];
    return (unsigned int)((end_off - adj_cur) >> 4);
}
