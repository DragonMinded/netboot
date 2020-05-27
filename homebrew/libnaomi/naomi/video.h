#ifndef __VIDEO_H
#define __VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#define POWERVR2_BASE 0xA05F8000

#define ID (0x000 >> 2)
#define REVISION (0x004 >> 2)
#define RESET (0x008 >> 2)
#define BORDER_COL (0x040 >> 2)
#define FB_DISPLAY_CFG (0x044 >> 2)
#define FB_RENDER_CFG (0x048 >> 2)
#define FB_RENDER_MODULO (0x04C >> 2)
#define FB_DISPLAY_ADDR_1 (0x050 >> 2)
#define FB_DISPLAY_ADDR_2 (0x054 >> 2)
#define FB_DISPLAY_SIZE (0x05C >> 2)
#define FB_ (0x054 >> 2)
#define FB_CLIP_X (0x068 >> 2)
#define FB_CLIP_Y (0x06C >> 2)
#define VRAM_CFG1 (0x0A0 >> 2)
#define VRAM_CFG3 (0x0A8 >> 2)
#define SYNC_LOAD (0x0D8 >> 2)
#define VBORDER (0x0DC >> 2)
#define TSP_CFG (0x0E4 >> 2)
#define VIDEO_CFG (0x0E8 >> 2)
#define HPOS (0x0EC >> 2)
#define VPOS (0x0F0 >> 2)
#define SYNC_CFG (0x0D0 >> 2)
#define SYNC_STAT (0x10C >> 2)

#define VRAM_BASE 0xA5000000

void video_wait_for_vblank();
unsigned int video_width();
unsigned int video_height();
void video_init_simple();
uint32_t rgb(unsigned int r, unsigned int g, unsigned int b);
void video_fill_screen(uint32_t color);
void video_fill_box(int x0, int y0, int x1, int y1, uint32_t color);
void video_draw_pixel(int x, int y, uint32_t color);
void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void video_draw_character(int x, int y, uint32_t color, char ch);
void video_draw_text(int x, int y, uint32_t color, const char * const msg);
void video_display();

#ifdef __cplusplus
}
#endif

#endif
