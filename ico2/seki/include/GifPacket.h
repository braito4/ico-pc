/*
 * ico2/seki/include/GifPacket.h
 *
 * The declarations of what GifPacket.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef GIFPACKET_H
#define GIFPACKET_H

/* a colour, four bytes in RGBA order: gif_DrawStripF and gif_DrawStripFST
 * take it by value, the sprite family and gif_Draw2DStripG by address;
 * weapon.c's dispBlur and puddle.c's drawRipple build it at their call
 * sites. */
typedef struct { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} GifColor; /* derived name */

/* a sprite's rectangle, a corner and a size, for the screen and for the
 * texture: the Sensitive forms take both in the GS's 1/16 units, the others
 * in whole pixels and texels, and the forms without Org scale the screen
 * rectangle from a 640 by 224 screen */
typedef struct { /* field names derived */
    int x;
    int y;
    int w;
    int h;
} GifRect; /* derived name */

int _IsInScreen(volatile int *v);
/* PC port: the native frame width (port/include/ico_frame.h, 512 at 4:3)
   the CPU strips' one-camera rule (GifPacket.c gifHostBeside) and the
   window build's GS register decoder read;
   GsBase.c's frame latch sets it at each flip, in every build */
void gif_HostSetFrameWidth(int w);
int gif_HostFrameWidth(void);
/* PC port (VIS-WINDOW, v0.6.1): whether the picture is not the PS2's out
   of the box (the Original preset on the 4:3 frame at 1x): set by GsBase.c's
   frame latch from the video options, true on any frame wider than 4:3;
   the CPU strips' window rule (GifPacket.c gifHostBeside) reads it */
void gif_HostSetExtended(int on);
int gif_HostExtended(void);
/* PC port (v0.6.0): the GIF_SP_FULL scope.  The 640-grid helpers (gif_Sprite,
   gif_SpriteSensitive and their Offset forms, gif_Point, gif_PointOffset,
   gif_Line, gif_LineOffset) map x to the centred 4:3 box: grid x * ScreenWidth
   / 640 (UI).  Between gif_FullFrameBegin and gif_FullFrameEnd they map it to
   the whole native frame, grid x * (gif_HostFrameWidth() / 640), so a 640-wide
   item covers the frame at every aspect; the window build tags the scope's
   primitives RD_SPACE_FULLFRAME.  y is unchanged.  Only an item that covers
   the frame by its layout data enters it (layout_texture.c: the primary
   sprite and the items whose grid rect spans 0 .. 640); never decided on
   pixels.  Scopes nest; at 4:3 the numbers are the UI helpers' own. */
void gif_FullFrameBegin(void);
void gif_FullFrameEnd(void);
int gif_CheckOpen(void);
void gif_Draw2DStripG(int *v, GifColor *col, int n, int prim);
void gif_DrawPolyF4(void *p0, void *p1, void *p2, void *p3, int r, int g, int b, int a, int prim);
void gif_DrawStripF(void *v, GifColor col, int n, int prim);
void gif_DrawStripFST(void *v, void *uv, GifColor col, int n, int prim);
void gif_EndPacket(void);
void gif_EndPacketPath1(void);
void gif_Init(void);
void gif_Line(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim);
void gif_MakeLine2D(int *v0, int *v1, long long z0, long long z1, unsigned char *col, int prim);
void gif_PointOffset(int *v, long long z, unsigned char *col, int prim);
void gif_SetAlpha(long long alpha, long long mode, long long fix);

void gif_SetDrawEnviroment(unsigned long long fbp, unsigned long long psm, unsigned int w,
                           unsigned int h, int useoffset, int clear);

void gif_SetGsReg(long long reg, long long data);
void gif_SetZTest(int on);
void gif_SetZWrite(int on);
void gif_Sprite(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim);
void gif_SpriteSensitive(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim);
void gif_SpriteSensitiveOffset(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim);
void gif_SpriteSensitiveOrg(GifRect *r, long long z, GifRect *uv, GifColor *col, int prim);
void gif_StartPacketPri(int pri);
void gif_StartPacketPriPath1(int pri);
void gif_MakeSpriteNoTexture(int x, int y, int w, int h, long long z, GifColor *col, int prim);

#endif /* GIFPACKET_H */
