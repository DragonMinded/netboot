#ifndef __VIDEO_H
#define __VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define POWERVR2_BASE 0xA05F8000

#define POWERVR2_ID (0x000 >> 2)
#define POWERVR2_REVISION (0x004 >> 2)
#define POWERVR2_RESET (0x008 >> 2)
#define POWERVR2_BORDER_COL (0x040 >> 2)
#define POWERVR2_FB_DISPLAY_CFG (0x044 >> 2)
#define POWERVR2_FB_RENDER_CFG (0x048 >> 2)
#define POWERVR2_FB_RENDER_MODULO (0x04C >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_1 (0x050 >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_2 (0x054 >> 2)
#define POWERVR2_FB_DISPLAY_SIZE (0x05C >> 2)
#define POWERVR2_FB_CLIP_X (0x068 >> 2)
#define POWERVR2_FB_CLIP_Y (0x06C >> 2)
#define POWERVR2_VRAM_CFG1 (0x0A0 >> 2)
#define POWERVR2_VRAM_CFG3 (0x0A8 >> 2)
#define POWERVR2_SYNC_LOAD (0x0D8 >> 2)
#define POWERVR2_VBORDER (0x0DC >> 2)
#define POWERVR2_TSP_CFG (0x0E4 >> 2)
#define POWERVR2_VIDEO_CFG (0x0E8 >> 2)
#define POWERVR2_HPOS (0x0EC >> 2)
#define POWERVR2_VPOS (0x0F0 >> 2)
#define POWERVR2_SYNC_CFG (0x0D0 >> 2)
#define POWERVR2_SYNC_STAT (0x10C >> 2)

#define DISPLAY_CFG_RGB1555 0
#define DISPLAY_CFG_RGB565 1
#define DISPLAY_CFG_RGB888 2
#define DISPLAY_CFG_RGB0888 3

#define RENDER_CFG_RGB0555 0
#define RENDER_CFG_RGB565 1
#define RENDER_CFG_ARGB4444 2
#define RENDER_CFG_ARGB1555 3
#define RENDER_CFG_RGB888 4
#define RENDER_CFG_RGB0888 5
#define RENDER_CFG_ARGB8888 6
// Mode 7 appears to be a redefinition of mode 2.

void video_wait_for_vblank();
unsigned int video_width();
unsigned int video_height();
void video_init_simple();
uint32_t rgb(unsigned int r, unsigned int g, unsigned int b);
uint32_t rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a);
void video_fill_screen(uint32_t color);
void video_set_background_color(uint32_t color);
void video_fill_box(int x0, int y0, int x1, int y1, uint32_t color);
void video_draw_pixel(int x, int y, uint32_t color);
void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void video_draw_character(int x, int y, uint32_t color, char ch);
void video_draw_text(int x, int y, uint32_t color, const char * const msg);
void video_draw_sprite( int x, int y, int width, int height, void *data );
void video_display();

#ifdef __cplusplus
}
#endif

#endif
