#ifndef __VIDEO_H
#define __VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "naomi/color.h"

// Note that all of the functions in here are intentionally not thread-safe.
// If you are attempting to update the video buffer from multiple threads at
// once it is indeterminate what will display on the screen. It is recommended
// not to interact with the video system across multiple threads.

// Defines for the color argument of the below function.
#define VIDEO_COLOR_1555 2
#define VIDEO_COLOR_8888 4

// Initialize the video hardware for software and hardware drawn sprites and
// graphics. Currently only supports 640x480@60fps VGA, no 15khz support.
// Pass one of the above video color defines to specify color depth.
void video_init(int colordepth);

// Free existing video system so that it can be initialized with another
// call.
void video_free();

// Wait for an appropriate time to swap buffers and then do so. Also fills
// the next screen's background with a previously set background color if
// a background color was set, while waiting for vblank to happen.
void video_display_on_vblank();

// Request that every frame be cleared to this color (use rgb() or
// rgba() to generate the color for this). Without this, you are
// responsible for clearing previous-frame garbage using video_fill_screen()
// or similar.
void video_set_background_color(color_t color);

// The width in pixels of the drawable video area. This could change
// depending on the monitor orientation.
unsigned int video_width();

// The height in pixels of the drawable video area. This could change
// depending on the monitor orientation.
unsigned int video_height();

// The depth in numer of bytes of the screen. You should expect this to
// be 2 or 4 depending on the video mode.
unsigned int video_depth();

// The current framebuffer that we are rendering to, for instances where
// you need direct access.
void *video_framebuffer();

// Scratch memory area in the VRAM region safe to modify without possibly
// corrupting video contents.
void *video_scratch_area();

// Returns nonzero if the screen is in vertical orientation, or zero if
// the screen is in horizontal orientation. This is for convenience, the
// pixel-based drawing functions always treat the top left of the screen
// as (0, 0) from the cabinet player's position.
unsigned int video_is_vertical();

// Fill the entire framebuffer with one color. Note that this is about
// 3x faster than doing it yourself as it uses hardware features to do so.
void video_fill_screen(color_t color);

// Given a staring and ending x and y coodinate, fills a simple box with
// the given color. This is orientation-aware.
void video_fill_box(int x0, int y0, int x1, int y1, color_t color);

// Given an x, y position and a color, colors that particular pixel with
// that particular color. This is orientation-aware.
void video_draw_pixel(int x, int y, color_t color);

// Given an x, y position, returns the color at that particular pixel. This
// returned color is suitable for passing into any function that requests
// a color parameter. This is orientation-aware.
color_t video_get_pixel(int x, int y);

// Given a starting and ending x and y coordinate, draws a line of a certain
// color between that starting and ending point. This is orientation-aware.
void video_draw_line(int x0, int y0, int x1, int y1, color_t color);

// Given a staring and ending x and y coodinate, draws a simple box with
// the given color. This is orientation-aware.
void video_draw_box(int x0, int y0, int x1, int y1, color_t color);

// Given an x, y coordinate, a sprite width and height, and a packed chunk
// of sprite data (should be video_depth() bytes per pixel in the sprite),
// draws the sprite to the screen at that x, y position. This is orientation
// aware and will skip drawing pixels with an alpha of 0.
void video_draw_sprite(int x, int y, int width, int height, void *data);

// Draw a debug character, string or formatted string of a certain color to
// the screen. This uses a built-in 8x8 fixed-width font and is always
// available regardless of other fonts or libraries. This is orientation aware.
// Also, video_draw_debug_text() takes a standard printf-formatted string with
// additional arguments. Note that this only supports ASCII printable characters.
void video_draw_debug_character(int x, int y, color_t color, char ch);
void video_draw_debug_text(int x, int y, color_t color, const char * const msg, ...);

// Include the freetype extensions for you, so you don't have to include video-freetype.h yourself.
#include "video-freetype.h"

#ifdef __cplusplus
}
#endif

#endif
