#ifdef FEATURE_FREETYPE
// Only build this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#include <stdint.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "naomi/video.h"
#include "naomi/font.h"
#include "video-internal.h"
#include "font-internal.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

font_cache_entry_t *__video_cache_create(uint32_t index, int advancex, int advancey, int bitmap_left, int bitmap_top, int width, int height, int mode, uint8_t *buffer)
{
    font_cache_entry_t *entry = malloc(sizeof(font_cache_entry_t));
    if (entry == 0)
    {
        return 0;
    }
    entry->index = index;
    entry->cache_namespace = FONT_CACHE_VIDEO;
    entry->advancex = advancex;
    entry->advancey = advancey;
    entry->bitmap_left = bitmap_left;
    entry->bitmap_top = bitmap_top;
    entry->mode = mode;
    entry->width = width;
    entry->height = height;
    entry->data = malloc(width * height);
    if (entry->data == 0)
    {
        free(entry);
        return 0;
    }
    if (mode == FT_PIXEL_MODE_GRAY)
    {
        memcpy(entry->data, buffer, width * height);
    }
    else
    {
        memset(entry->data, 0, width * height);
    }

    return entry;
}

extern unsigned int cached_actual_width;
extern unsigned int cached_actual_height;
extern unsigned int global_video_depth;
extern unsigned int global_video_vertical;
extern unsigned int global_video_width;
extern void * buffer_base;

void __video_draw_cached_bitmap(int x, int y, unsigned int width, unsigned int height, void *data, color_t color)
{
    uint8_t *buffer = data;
    int low_x = 0;
    int high_x = width;
    int low_y = 0;
    int high_y = height;

    if (x < 0)
    {
        if (x + width <= 0)
        {
            return;
        }

        low_x = -x;
    }
    if (y < 0)
    {
        if (y + height <= 0)
        {
            return;
        }

        low_y = -y;
    }
    if ((x + width) >= cached_actual_width)
    {
        if (x >= cached_actual_width)
        {
            return;
        }

        high_x = cached_actual_width - x;
    }
    if (y + height >= cached_actual_height)
    {
        if (y >= cached_actual_height)
        {
            return;
        }

        high_y = cached_actual_height - y;
    }

    // The below algorithm is fully duplicated for speed. It makes a massive difference
    // (on the order of 33% faster) so it is worth the code duplication.
    if (global_video_depth == 2)
    {
        // Grab the color itself.
        unsigned int sr = color.r;
        unsigned int sg = color.g;
        unsigned int sb = color.b;
        uint32_t actual_color = RGB0555(color.r, color.g, color.b);

        if (global_video_vertical)
        {
            /* Iterate slightly differently so we can guarantee that we're close to the data
             * cache, since drawing vertically is done from the perspective of a horizontal
             * buffer. */
            for(int xp = low_x; xp < high_x; xp++)
            {
                for (int yp = (high_y - 1); yp >= low_y; yp--)
                {
                    // Alpha-blend the grayscale image with the destination.
                    // We only support 32 alpha levels here for speed.
                    unsigned int alpha = buffer[(yp * width) + xp] | 0x7;

                    if (alpha > 0x7)
                    {
                        if(alpha >= 255)
                        {
                            SET_PIXEL_V_2(buffer_base, x + xp, y + yp, actual_color);
                        }
                        else
                        {
                            unsigned int dr;
                            unsigned int dg;
                            unsigned int db;
                            unsigned int negalpha = (~alpha) & 0xFF;

                            // Technically it should be divided by 255, but this should
                            // be much much faster for an 0.4% accuracy loss.
                            EXPLODE0555(GET_PIXEL_V_2(buffer_base, x + xp, y + yp), dr, dg, db);
                            dr = ((sr * alpha) + (dr * negalpha)) >> 8;
                            dg = ((sg * alpha) + (dg * negalpha)) >> 8;
                            db = ((sb * alpha) + (db * negalpha)) >> 8;
                            SET_PIXEL_V_2(buffer_base, x + xp, y + yp, RGB0555(dr, dg, db));
                        }
                    }
                }
            }
        }
        else
        {
            for (int yp = low_y; yp < high_y; yp++)
            {
                for(int xp = low_x; xp < high_x; xp++)
                {
                    // Alpha-blend the grayscale image with the destination.
                    // We only support 32 alpha levels here for speed.
                    unsigned int alpha = buffer[(yp * width) + xp] | 0x7;

                    if (alpha > 0x7)
                    {
                        if(alpha >= 255)
                        {
                            SET_PIXEL_H_2(buffer_base, x + xp, y + yp, actual_color);
                        }
                        else
                        {
                            unsigned int dr;
                            unsigned int dg;
                            unsigned int db;
                            unsigned int negalpha = (~alpha) & 0xFF;

                            // Technically it should be divided by 255, but this should
                            // be much much faster for an 0.4% accuracy loss.
                            EXPLODE0555(GET_PIXEL_H_2(buffer_base, x + xp, y + yp), dr, dg, db);
                            dr = ((sr * alpha) + (dr * negalpha)) >> 8;
                            dg = ((sg * alpha) + (dg * negalpha)) >> 8;
                            db = ((sb * alpha) + (db * negalpha)) >> 8;
                            SET_PIXEL_H_2(buffer_base, x + xp, y + yp, RGB0555(dr, dg, db));
                        }
                    }
                }
            }
        }
    }
    else if (global_video_depth == 4)
    {
        // Grab the color itself.
        unsigned int sr = color.r;
        unsigned int sg = color.g;
        unsigned int sb = color.b;
        uint32_t actual_color = RGB0888(color.r, color.g, color.b);

        if (global_video_vertical)
        {
            /* Iterate slightly differently so we can guarantee that we're close to the data
             * cache, since drawing vertically is done from the perspective of a horizontal
             * buffer. */
            for(int xp = low_x; xp < high_x; xp++)
            {
                for (int yp = (high_y - 1); yp >= low_y; yp--)
                {
                    // Alpha-blend the grayscale image with the destination.
                    // We only support 32 alpha levels here for speed.
                    unsigned int alpha = buffer[(yp * width) + xp] | 0x7;

                    if (alpha > 0x7)
                    {
                        if(alpha >= 255)
                        {
                            SET_PIXEL_V_4(buffer_base, x + xp, y + yp, actual_color);
                        }
                        else
                        {
                            unsigned int dr;
                            unsigned int dg;
                            unsigned int db;
                            unsigned int negalpha = (~alpha) & 0xFF;

                            // Technically it should be divided by 255, but this should
                            // be much much faster for an 0.4% accuracy loss.
                            EXPLODE0888(GET_PIXEL_V_4(buffer_base, x + xp, y + yp), dr, dg, db);
                            dr = ((sr * alpha) + (dr * negalpha)) >> 8;
                            dg = ((sg * alpha) + (dg * negalpha)) >> 8;
                            db = ((sb * alpha) + (db * negalpha)) >> 8;
                            SET_PIXEL_V_4(buffer_base, x + xp, y + yp, RGB0888(dr, dg, db));
                        }
                    }
                }
            }
        }
        else
        {
            for (int yp = low_y; yp < high_y; yp++)
            {
                for(int xp = low_x; xp < high_x; xp++)
                {
                    // Alpha-blend the grayscale image with the destination.
                    // We only support 32 alpha levels here for speed.
                    unsigned int alpha = buffer[(yp * width) + xp] | 0x7;

                    if (alpha > 0x7)
                    {
                        if(alpha >= 255)
                        {
                            SET_PIXEL_H_4(buffer_base, x + xp, y + yp, actual_color);
                        }
                        else
                        {
                            unsigned int dr;
                            unsigned int dg;
                            unsigned int db;
                            unsigned int negalpha = (~alpha) & 0xFF;

                            // Technically it should be divided by 255, but this should
                            // be much much faster for an 0.4% accuracy loss.
                            EXPLODE0888(GET_PIXEL_H_4(buffer_base, x + xp, y + yp), dr, dg, db);
                            dr = ((sr * alpha) + (dr * negalpha)) >> 8;
                            dg = ((sg * alpha) + (dg * negalpha)) >> 8;
                            db = ((sb * alpha) + (db * negalpha)) >> 8;
                            SET_PIXEL_H_4(buffer_base, x + xp, y + yp, RGB0888(dr, dg, db));
                        }
                    }
                }
            }
        }
    }
}

// Given how this module caches, both the uncached and cached draw functions are identical.
void __video_draw_uncached_bitmap(int x, int y, unsigned int width, unsigned int height, uint8_t *data, color_t color)
{
    __video_draw_cached_bitmap(x, y, width, height, data, color);
}

int video_draw_character(int x, int y, font_t *fontface, color_t color, int ch)
{
    return _font_draw_calc_character(x, y, fontface, color, ch, 0, &__video_cache_create, FONT_CACHE_VIDEO, &__video_draw_uncached_bitmap, &__video_draw_cached_bitmap);
}

int video_draw_text(int x, int y, font_t *fontface, color_t color, const char * const msg, ...)
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
            return _font_draw_calc_text(x, y, fontface, color, buffer, 0, &__video_cache_create, FONT_CACHE_VIDEO, &__video_draw_uncached_bitmap, &__video_draw_cached_bitmap);
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
