#ifdef FEATURE_FREETYPE
// Only provide this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#ifndef __TA_FREETYPE_H
#define __TA_FREETYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "naomi/font.h"
#include "naomi/color.h"

// API that can be used with the PVR library as well as the freetype
// library to render text to the screen.

// Given a previously set up font, draw a character. Unlike the debug
// character draw routines, this is unicode aware. It is also monitor
// orientation aware.
int ta_draw_character(int x, int y, font_t *fontface, color_t color, int ch);

// Given a previously set up font, draw a string. Unlike the debug
// character draw routines, this is unicode aware. It is also monitor
// orientation aware. It also takes standard printf-style format strings.
int ta_draw_text(int x, int y, font_t *fontface, color_t color, const char * const msg, ...);

#ifdef __cplusplus
}
#endif

#endif
#endif
