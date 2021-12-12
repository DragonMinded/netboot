#ifdef FEATURE_FREETYPE
// Only provide this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#ifndef __FONT_H
#define __FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define FONT_CACHE_SIZE 1024
#define MAX_FALLBACK_SIZE 10

typedef struct
{
    void **faces;
    unsigned int lineheight;
    void **cache;
    unsigned int cachesize;
    unsigned int cacheloc;
} font_t;

typedef struct
{
    unsigned int width;
    unsigned int height;
} font_metrics_t;

// API that can be used to load and interact with fonts using the freetype library.

// Load a new fontface and return a handle to it. If there was not enough memory for
// this fontface, returns a null pointer, so be sure to check for that.
font_t *font_add(void *buffer, unsigned int size);

// Discard a previously loaded fontface.
void font_disard(font_t *fontface);

// Add a fallback fontface to a previously created font for rendering
// characters that do not appear in the original font. Returns zero
// on success or a negative error value on failure.
int font_add_fallback(font_t *fontface, void *buffer, unsigned int size);

// Set the pixel size for a particular font. Returns zero on success
// or a negative error value on failure.
int font_set_size(font_t *fontface, unsigned int size);

// Given a previously set up font, return the metrics for a character.
// Much like the above draw character function, this is unicode aware.
font_metrics_t font_get_character_metrics(font_t *fontface, int ch);

// Given a previously set up font, return the metrics for a string.
// Much like the above draw text function, this is unicode aware.
font_metrics_t font_get_text_metrics(font_t *fontface, const char *msg, ...);

#ifdef __cplusplus
}
#endif

#endif
#endif
