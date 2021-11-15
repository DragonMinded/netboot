#include "video.h"

/* Initialize the PVR subsystem to a known state */

/* These values mainly from Dans 3dtest program... */

static unsigned int three_d_params[] = {
	0x80a8, 0x15d1c951,	/* M (Unknown magic value) */
	0x80a0, 0x00000020,	/* M */
	0x8008, 0x00000000,	/* TA out of reset */
	0x8048, 0x00000009,	/* alpha config */
	0x8068, 0x02800000,	/* pixel clipping x */
	0x806c, 0x01e00000,	/* pixel clipping y */
	0x8110, 0x00093f39,	/* M */
	0x8098, 0x00800408,	/* M */
	0x804c, 0x000000a0,	/* display align (640*2)/8 */
	0x8078, 0x3f800000,	/* polygon culling (1.0f) */
	0x8084, 0x00000000,	/* M */
	0x8030, 0x00000101,	/* M */
	0x80b0, 0x007f7f7f,	/* Fog table color */
	0x80b4, 0x007f7f7f,	/* Fog vertex color */
	0x80c0, 0x00000000,	/* color clamp min */
	0x80bc, 0xffffffff,	/* color clamp max */
	0x8080, 0x00000007,	/* M */
	0x8074, 0x00000001,	/* cheap shadow */
	0x807c, 0x0027df77,	/* M */
	0x8008, 0x00000001,	/* TA reset */
	0x8008, 0x00000000,	/* TA out of reset */
	0x80e4, 0x00000000,	/* stride width */
	0x80b8, 0x0000ff07,	/* fog density */
	0x80b4, 0x007f7f7f,	/* fog vertex color */
	0x80b0, 0x007f7f7f,	/* fog table color */
	0x8108, 0x00000003      /* 32bit palette  */
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
