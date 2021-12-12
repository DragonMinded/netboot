#ifndef __FONT_INTERNAL_H
#define __FONT_INTERNAL_H

#include "naomi/video.h"

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
    void *data;
} font_cache_entry_t;

typedef font_cache_entry_t * (*cache_func_t)(uint32_t index, int advancex, int advancey, int bitmap_left, int bitmap_top, int width, int height, int mode, uint8_t *buffer);
typedef void (*cached_draw_func_t)(int x, int y, unsigned int width, unsigned int height, unsigned int mode, void *data, color_t color);
typedef void (*uncached_draw_func_t)(int x, int y, unsigned int width, unsigned int height, unsigned int mode, uint8_t *data, color_t color);

int _font_draw_calc_character(
    int x,
    int y,
    font_t *fontface,
    color_t color,
    int ch,
    font_metrics_t *metrics,
    cache_func_t cache_func,
    uncached_draw_func_t uncached_draw,
    cached_draw_func_t cached_draw
);
int _font_draw_calc_text(
    int x,
    int y,
    font_t *fontface,
    color_t color,
    const char * const msg,
    font_metrics_t *metrics,
    cache_func_t cache_func,
    uncached_draw_func_t uncached_draw,
    cached_draw_func_t cached_draw
);

#endif
