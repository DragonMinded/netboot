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

// Initialize a simple video setup, currently only supporting 640x480
// RGB 1555 color.
void video_init_simple();

// Wait for an appropriate time to call video_display(). Also polls
// for dimm communications. Also fills the next screen's background
// with a previously set background color if a background color was
// set.
void video_wait_for_vblank();

// Actually swap framebuffers. If you have set a background color, this
// also finishes filling the next screen's background with that color.
void video_display();

// Request that every frame be cleared to this color (use rgb() or
// rgba() to generate the color for this). Without this, you are
// responsible for clearing previous-frame garbage using video_fill_screen()
// or similar.
void video_set_background_color(uint32_t color);

// The width in pixels of the drawable video area. This could change
// depending on the monitor orientation.
unsigned int video_width();

// The height in pixels of the drawable video area. This could change
// depending on the monitor orientation.
unsigned int video_height();

// The depth in numer of bytes of the screen. You should expect this to
// be 2 or 4 depending on the video mode.
unsigned int video_depth();

// Scratch memory area in the VRAM region safe to modify without possibly
// corrupting video contents.
void *video_scratch_area();

// Returns nonzero if the screen is in vertical orientation, or zero if
// the screen is in horizontal orientation. This is for convenience, the
// pixel-based drawing functions always treat the top left of the screen
// as (0, 0) from the cabinet player's position.
unsigned int video_is_vertical();

// Generates a color uint32 suitable for passing into any function that
// requests a color parameter.
uint32_t rgb(unsigned int r, unsigned int g, unsigned int b);
uint32_t rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a);

// Ungenerates a rgb or rgba set from a color uint32 that was made with
// the above functions or retrieved using video_get_pixel.
void explodergb(uint32_t color, unsigned int *r, unsigned int *g, unsigned int *b);
void explodergba(uint32_t color, unsigned int *r, unsigned int *g, unsigned int *b, unsigned int *a);

// Fill the entire framebuffer with one color. Note that this is about
// 3x faster than doing it yourself as it uses hardware features to do so.
void video_fill_screen(uint32_t color);

// Given a staring and ending x and y coodinate, fills a simple box with
// the given color. This is orientation-aware.
void video_fill_box(int x0, int y0, int x1, int y1, uint32_t color);

// Given an x, y position and a color, colors that particular pixel with
// that particular color. This is orientation-aware.
void video_draw_pixel(int x, int y, uint32_t color);

// Given an x, y position, returns the color at that particular pixel. This
// returned color is suitable for passing into any function that requests
// a color parameter. This is orientation-aware.
uint32_t video_get_pixel(int x, int y);

// Given a starting and ending x and y coordinate, draws a line of a certain
// color between that starting and ending point. This is orientation-aware.
void video_draw_line(int x0, int y0, int x1, int y1, uint32_t color);

// Given a staring and ending x and y coodinate, draws a simple box with
// the given color. This is orientation-aware.
void video_draw_box(int x0, int y0, int x1, int y1, uint32_t color);

// Given an x, y coordinate, a sprite width and height, and a packed chunk
// of sprite data (should be video_depth() bytes per pixel in the sprite),
// draws the sprite to the screen at that x, y position. This is orientation
// aware and will skip drawing pixels with an alpha of 0.
void video_draw_sprite( int x, int y, int width, int height, void *data);

// Draw a debug character, string or formatted string of a certain color to
// the screen. This uses a built-in 8x8 fixed-width font and is always
// available regardless of other fonts or libraries. This is orientation aware.
// Also, video_draw_debug_text() takes a standard printf-formatted string with
// additional arguments. Note that this only supports ASCII printable characters.
void video_draw_debug_character(int x, int y, uint32_t color, char ch);
void video_draw_debug_text(int x, int y, uint32_t color, const char * const msg, ...);

#include "video-freetype.h"

#ifdef __cplusplus
}
#endif

#endif
