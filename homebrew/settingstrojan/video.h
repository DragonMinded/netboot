#ifndef __VIDEO_H
#define __VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void video_init_simple();
void video_free();
void video_wait_for_vblank();
void video_display_on_vblank();
uint32_t rgb(unsigned int r, unsigned int g, unsigned int b);
void video_fill_screen(uint32_t color);
void video_draw_pixel(int x, int y, uint32_t color);
void video_draw_debug_character(int x, int y, uint32_t color, char ch);
void video_draw_debug_text(int x, int y, uint32_t color, const char * const msg, ...);

#define SET_PIXEL_V_2(base, x, y, color) ((uint16_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)] = (color) & 0xFFFF
#define SET_PIXEL_H_2(base, x, y, color) ((uint16_t *)(base))[(x) + ((y) * global_video_width)] = (color) & 0xFFFF
#define SET_PIXEL_V_4(base, x, y, color) ((uint32_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)] = (color)
#define SET_PIXEL_H_4(base, x, y, color) ((uint32_t *)(base))[(x) + ((y) * global_video_width)] = (color)

#define RGB0555(r, g, b) ((((b) >> 3) & (0x1F << 0)) | (((g) << 2) & (0x1F << 5)) | (((r) << 7) & (0x1F << 10)) | 0x8000)
#define RGB1555(r, g, b, a) ((((b) >> 3) & (0x1F << 0)) | (((g) << 2) & (0x1F << 5)) | (((r) << 7) & (0x1F << 10)) | (((a) << 8) & 0x8000))

#endif
