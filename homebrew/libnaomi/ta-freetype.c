#ifdef FEATURE_FREETYPE
// Only build this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#include <stdint.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "naomi/video.h"
#include "naomi/ta.h"
#include "naomi/font.h"
#include "video-internal.h"
#include "font-internal.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

font_cache_entry_t *_ta_cache_create(uint32_t index, int advancex, int advancey, int bitmap_left, int bitmap_top, int width, int height, int mode, uint8_t *buffer)
{
    font_cache_entry_t *entry = malloc(sizeof(font_cache_entry_t));
    if (entry == 0)
    {
        return 0;
    }
    entry->index = index;
    entry->advancex = advancex;
    entry->advancey = advancey;
    entry->bitmap_left = bitmap_left;
    entry->bitmap_top = bitmap_top;
    entry->mode = mode;
    entry->width = width;
    entry->height = height;
    // TODO Point this at a tracking structure for sprite maps.
    entry->data = malloc(4);
    if (entry->data == 0)
    {
        free(entry);
        return 0;
    }
    memcpy(entry->data, buffer, 4);

    return entry;
}

extern unsigned int cached_actual_width;
extern unsigned int cached_actual_height;
extern unsigned int global_video_depth;
extern unsigned int global_video_vertical;
extern unsigned int global_video_width;
extern void * buffer_base;

void _ta_draw_bitmap(int x, int y, unsigned int width, unsigned int height, unsigned int mode, void *data, color_t color)
{
    // TODO: Implement this!
}

int ta_draw_character(int x, int y, font_t *fontface, color_t color, int ch)
{
    return _font_draw_calc_character(x, y, fontface, color, ch, 0, &_ta_cache_create, &_ta_draw_bitmap);
}

int ta_draw_text(int x, int y, font_t *fontface, color_t color, const char * const msg, ...)
{
    if (msg)
    {
        char buffer[2048];
        va_list args;
        va_start(args, msg);
        int length = vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        if (length > 0)
        {
            buffer[min(length, 2047)] = 0;
            return _font_draw_calc_text(x, y, fontface, color, buffer, 0, &_ta_cache_create, &_ta_draw_bitmap);
        }
        else if (length == 0)
        {
            return 0;
        }
        else
        {
            return -1;
        }
    }
    else
    {
        return 0;
    }
}
#endif
