#include "video.h"

/* Initialize the PVR subsystem to a known state */

/* These values mainly from Dans 3dtest program... */

static unsigned int three_d_params[] = {
	0x8098, 0x00800408,	/* Polygon sorting and cache sizes */
	0x8078, 0x3f800000,	/* Polygon culling (1.0f) */
	0x8084, 0x00000000,	/* Perpendicular triangle compare (0.0f) */
	0x8030, 0x00000101,	/* Span sorting enable */
	0x80b0, 0x007f7f7f,	/* Fog table color (ARGB, A is ignored) */
	0x80b4, 0x007f7f7f,	/* Fog vertex color (ARGB, A is ignored) */
	0x80c0, 0x00000000,	/* Color clamp min (ARGB) */
	0x80bc, 0xffffffff,	/* Color clamp max (ARGB) */
	0x8080, 0x00000007,	/* Pixel sampling position, everything set at (0.5, 0.5) */
	0x8074, 0x00000000,	/* Shadow scaling */
	0x807c, 0x0027df77,	/* FPU params? */
	0x8008, 0x00000001,	/* TA reset */
	0x8008, 0x00000000,	/* TA out of reset */
	0x80e4, 0x00000000,	/* stride width (TSP_CFG) */
	0x80b8, 0x0000ff07,	/* fog density */
	0x80b4, 0x007f7f7f,	/* fog vertex color */
	0x80b0, 0x007f7f7f,	/* fog table color */
	0x8108, 0x00000003  /* 32bit palette (0x0 = ARGB1555, 0x1 = RGB565, 0x2 = ARGB4444, 0x3 = ARGB8888  */
};

static void set_regs(unsigned int *values, int cnt)
{
  volatile unsigned char *regs = (volatile unsigned char *)(void *)0xa05f0000;
  unsigned int r, v;
  
  while(cnt--) {
    r = *values++;
    v = *values++;
    *(volatile unsigned int *)(regs+r) = v;
  }
}

void init_pvr()
{
  volatile unsigned int *vbl = (volatile unsigned int *)(void *)0xa05f810c;

  set_regs(three_d_params, sizeof(three_d_params)/sizeof(three_d_params[0])/2);
  // Wait for vblank.
  while (!(*vbl & 0x01ff));
  while (*vbl & 0x01ff);
}
