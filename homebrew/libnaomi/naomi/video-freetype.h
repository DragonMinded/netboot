#if __has_include(<ft2build.h>)
// Only provide this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#ifndef __VIDEO_FREETYPE_H
#define __VIDEO_FREETYPE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FONT_CACHE_SIZE 1024
#define MAX_FALLBACK_SIZE 10

typedef struct
{
    uint32_t index;
    int advancex;
    int advancey;
    int bitmap_left;
    int bitmap_top;
    int width;
    int height;
    int mode;
    uint8_t *buffer;
} font_cache_entry_t;

typedef struct
{
    void **faces;
    unsigned int lineheight;
    font_cache_entry_t **cache;
    unsigned int cachesize;
    unsigned int cacheloc;
} font_t;

typedef struct
{
    unsigned int width;
    unsigned int height;
} font_metrics_t;

// API that can be used with the video library as well as the freetype
// library to render text to the screen.

// Load a new fontface and return a handle to it.
font_t *video_font_add(void *buffer, unsigned int size);

// Discard a previously loaded fontface.
void video_font_disard(font_t *fontface);

// Add a fallback fontface to a previously created font for rendering
// characters that do not appear in the original font.
int video_font_add_fallback(font_t *fontface, void *buffer, unsigned int size);

// Set the pixel size for a particular font
int video_font_set_size(font_t *fontface, unsigned int size);

// Given a previously set up font, draw a character. Unlike the debug
// character draw routines, this is unicode aware. It is also monitor
// orientation aware.
int video_draw_character(int x, int y, font_t *fontface, uint32_t color, int ch);

// Given a previously set up font, return the metrics for a character.
// Much like the above draw character function, this is unicode aware.
font_metrics_t video_get_character_metrics(font_t *fontface, int ch);

// Given a previously set up font, draw a string. Unlike the debug
// character draw routines, this is unicode aware. It is also monitor
// orientation aware. It also takes standard printf-style format strings.
int video_draw_text(int x, int y, font_t *fontface, uint32_t color, const char * const msg, ...);

// Given a previously set up font, return the metrics for a string.
// Much like the above draw text function, this is unicode aware.
font_metrics_t video_get_text_metrics(font_t *fontface, const char *msg, ...);

#ifdef __cplusplus
}
#endif

#endif
#endif
