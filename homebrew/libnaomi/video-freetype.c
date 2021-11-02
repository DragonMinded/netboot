#if __has_include(<ft2build.h>)
// Only build this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#include <stdint.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "naomi/system.h"
#include "naomi/video.h"
#include "video-internal.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

FT_Library * __video_freetype_init()
{
    static FT_Library library;
    static int init = 0;

    if (!init)
    {
        FT_Init_FreeType(&library);
    }

    return &library;
}

font_t * video_font_add(void *buffer, unsigned int size)
{
    FT_Library *library = __video_freetype_init();
    font_t *font = malloc(sizeof(font_t));
    font->faces = malloc(sizeof(void *) * MAX_FALLBACK_SIZE);
    memset(font->faces, 0, sizeof(void *) * MAX_FALLBACK_SIZE);
    font->faces[0] = malloc(sizeof(FT_Face));

    if (FT_New_Memory_Face(*library, buffer, size, 0, (FT_Face *)font->faces[0]))
    {
        free(font->faces[0]);
        free(font->faces);
        free(font);
        return 0;
    }
    FT_Select_Charmap(*((FT_Face *)font->faces[0]), FT_ENCODING_UNICODE);

    font->cachesize = FONT_CACHE_SIZE;
    font->cacheloc = 0;
    font->cache = malloc(sizeof(font_cache_entry_t *) * font->cachesize);
    memset(font->cache, 0, sizeof(font_cache_entry_t *) * font->cachesize);

    video_font_set_size(font, 12);

    return font;
}

int video_font_add_fallback(font_t *font, void *buffer, unsigned int size)
{
    for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
    {
        if (font->faces[i] == 0)
        {
            FT_Library *library = __video_freetype_init();
            font->faces[i] = malloc(sizeof(FT_Face));
            int error = FT_New_Memory_Face(*library, buffer, size, 0, (FT_Face *)font->faces[i]);
            if (error)
            {
                free(font->faces[i]);
                font->faces[i] = 0;
                return error;
            }
            FT_Select_Charmap(*((FT_Face *)font->faces[i]), FT_ENCODING_UNICODE);
            video_font_set_size(font, font->lineheight);

            return 0;
        }
    }

    return -1;
}

void __cache_discard(font_t *fontface)
{
    for (int i = 0; i < fontface->cacheloc; i++)
    {
        free(fontface->cache[i]->buffer);
        free(fontface->cache[i]);
        fontface->cache[i] = 0;
    }

    fontface->cacheloc = 0;
}

font_cache_entry_t *__cache_lookup(font_t *fontface, uint32_t index)
{
    // TODO: This is linear and we could make it a lot better if we sorted
    // by index and then did a binary search. The lion's share of this
    // module's compute time goes to __draw_bitmap however, so I didn't bother.

    for (int i = 0; i < fontface->cacheloc; i++)
    {
        if(fontface->cache[i]->index == index)
        {
            return fontface->cache[i];
        }
    }

    return 0;
}

int __cache_add(font_t *fontface, font_cache_entry_t *entry)
{
    if (fontface->cacheloc == fontface->cachesize)
    {
        return 0;
    }

    fontface->cache[fontface->cacheloc++] = entry;
    return 1;
}

font_cache_entry_t *__cache_create(uint32_t index, int advancex, int advancey, int bitmap_left, int bitmap_top, int width, int height, int mode, uint8_t *buffer)
{
    font_cache_entry_t *entry = malloc(sizeof(font_cache_entry_t));
    entry->index = index;
    entry->advancex = advancex;
    entry->advancey = advancey;
    entry->bitmap_left = bitmap_left;
    entry->bitmap_top = bitmap_top;
    entry->mode = mode;
    entry->width = width;
    entry->height = height;
    entry->buffer = malloc(width * height);
    memcpy(entry->buffer, buffer, width * height);

    return entry;
}

void video_font_discard(font_t *fontface)
{
    if (fontface)
    {
        for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
        {
            if (fontface->faces[i] != 0)
            {
                FT_Done_Face(*((FT_Face *)fontface->faces[i]));
                free(fontface->faces[i]);
            }
        }
        __cache_discard(fontface);
        free(fontface->cache);
        free(fontface->faces);
        free(fontface);
    }
}

int video_font_set_size(font_t *fontface, unsigned int size)
{
    if (fontface)
    {
        for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
        {
            if (fontface->faces[i] != 0)
            {
                int error = FT_Set_Pixel_Sizes(*((FT_Face *)fontface->faces[i]), 0, size);
                if (error)
                {
                    return error;
                }
            }
        }

        fontface->lineheight = size;
        __cache_discard(fontface);

        return 0;
    }
    else
    {
        return -1;
    }
}

extern unsigned int cached_actual_width;
extern unsigned int cached_actual_height;
extern unsigned int global_video_depth;
extern unsigned int global_video_vertical;
extern unsigned int global_video_width;
extern void * buffer_base;

void __draw_bitmap(int x, int y, unsigned int width, unsigned int height, unsigned int mode, uint8_t *buffer, uint32_t color)
{
    if (mode == FT_PIXEL_MODE_GRAY)
    {
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

        // Grab the color itself.
        unsigned int sr;
        unsigned int sg;
        unsigned int sb;
        EXPLODE0555(color, sr, sg, sb);

        // The below algorithm is fully duplicated for speed. It makes a massive difference
        // (on the order of 33% faster) so it is worth the code duplication.
        if (global_video_depth == 2)
        {
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
                                SET_PIXEL_V_2(buffer_base, x + xp, y + yp, color);
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
                                SET_PIXEL_H_2(buffer_base, x + xp, y + yp, color);
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
            // TODO: Need to support 32-bit video modes.
        }
    }
    else
    {
        // TODO: Need to support bitmapped fonts.
    }
}

int __video_draw_calc_character(int x, int y, font_t *fontface, uint32_t color, int ch, font_metrics_t *metrics, unsigned int draw)
{
    if (fontface)
    {
        font_cache_entry_t *entry = __cache_lookup(fontface, ch);
        unsigned int lineheight = fontface->lineheight;

        if (entry)
        {
            x += entry->bitmap_left;
            y += lineheight - entry->bitmap_top;

            if (draw)
            {
                __draw_bitmap(x, y, entry->width, entry->height, entry->mode, entry->buffer, color);
            }
            if (metrics)
            {
                metrics->width = entry->advancex;
                metrics->height = lineheight;
            }
        }
        else
        {
            // Grab the actual unicode glyph, searching through all fallbacks if we need to.
            // faces[0] is always guaranteed to be valid, since that's our original non-fallback
            // fontface. If none of the fonts has this glyph, then we fall back even further to
            // the original font selected, and display the unicode error glyph.
            FT_Face *face = (FT_Face *)fontface->faces[0];
            for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
            {
                if (fontface->faces[i] != 0)
                {
                    FT_UInt glyph_index = FT_Get_Char_Index( *((FT_Face *)fontface->faces[i]), ch );
                    if (glyph_index != 0)
                    {
                        // This font has this glyph. Use this instead of the original.
                        face = (FT_Face *)fontface->faces[i];
                        break;
                    }
                }
            }
            int error = FT_Load_Char(*face, ch, FT_LOAD_RENDER);
            if (error)
            {
                return error;
            }

            // Copy it out onto our buffer.
            FT_GlyphSlot slot = (*face)->glyph;
            x += slot->bitmap_left;
            y += lineheight - slot->bitmap_top;

            if (draw)
            {
                // Alpha-composite the grayscale bitmap, treating it as an alpha map.
                __draw_bitmap(x, y, slot->bitmap.width, slot->bitmap.rows, slot->bitmap.pixel_mode, slot->bitmap.buffer, color);
            }
            if (metrics)
            {
                metrics->width = slot->advance.x >> 6;
                metrics->height = lineheight;
            }

            // Add it to the cache so we can render faster next time.
            if (fontface->cacheloc < fontface->cachesize)
            {
                entry = __cache_create(
                    ch,
                    slot->advance.x >> 6,
                    slot->advance.y >> 6,
                    slot->bitmap_left,
                    slot->bitmap_top,
                    slot->bitmap.width,
                    slot->bitmap.rows,
                    slot->bitmap.pixel_mode,
                    slot->bitmap.buffer
                 );
                __cache_add(fontface, entry);
            }
        }

        return 0;
    }
    else
    {
        return -1;
    }
}

int __video_draw_calc_text(int x, int y, font_t *fontface, uint32_t color, const char * const msg, font_metrics_t *metrics, unsigned int draw)
{
    if (metrics)
    {
        metrics->width = 0;
        metrics->height = 0;
    }
    if( msg == 0 ) { return 0; }

    int tx = x;
    int ty = y;
    uint32_t *text = utf8_convert(msg);

    if (text)
    {
        uint32_t *freeptr = text;
        unsigned int lineheight = fontface->lineheight;

        while( *text )
        {
            switch( *text )
            {
                case '\r':
                case '\n':
                {
                    if (metrics)
                    {
                        // Make sure to remember the maximum line width for this line.
                        metrics->width = metrics->width > tx ? metrics->width : tx;
                        metrics->height = ty + lineheight;
                    }
                    tx = x;
                    ty += lineheight;
                    break;
                }
                case '\t':
                {
                    font_cache_entry_t *entry = __cache_lookup(fontface, *text);
                    if (entry)
                    {
                        tx += entry->advancex * 5;
                        tx += entry->advancey * 5;
                    }
                    else
                    {
                        // Every font should have a space, I'm not doing a fallack for that one.
                        FT_Face *face = (FT_Face *)fontface->faces[0];
                        int error = FT_Load_Char(*face, ' ', FT_LOAD_RENDER);
                        if (error)
                        {
                            return error;
                        }

                        FT_GlyphSlot slot = (*face)->glyph;
                        tx += (slot->advance.x >> 6) * 5;
                        ty += (slot->advance.y >> 6) * 5;

                        // Add it to the cache so we can render faster next time.
                        if (fontface->cacheloc < fontface->cachesize)
                        {
                            entry = __cache_create(
                                *text,
                                slot->advance.x >> 6,
                                slot->advance.y >> 6,
                                slot->bitmap_left,
                                slot->bitmap_top,
                                slot->bitmap.width,
                                slot->bitmap.rows,
                                slot->bitmap.pixel_mode,
                                slot->bitmap.buffer
                             );
                            __cache_add(fontface, entry);
                        }
                    }
                    if (metrics)
                    {
                        metrics->width = metrics->width > tx ? metrics->width : tx;
                        metrics->height = ty + lineheight;
                    }
                    break;
                }
                default:
                {
                    font_cache_entry_t *entry = __cache_lookup(fontface, *text);
                    if (entry)
                    {
                        if (draw)
                        {
                            __draw_bitmap(tx + entry->bitmap_left, ty + lineheight - entry->bitmap_top, entry->width, entry->height, entry->mode, entry->buffer, color);
                        }

                        // Advance the pen based on this glyph.
                        tx += entry->advancex;
                        ty += entry->advancey;
                    }
                    else
                    {
                        // Grab the actual unicode glyph, searching through all fallbacks if we need to.
                        // faces[0] is always guaranteed to be valid, since that's our original non-fallback
                        // fontface. If none of the fonts has this glyph, then we fall back even further to
                        // the original font selected, and display the unicode error glyph.
                        FT_Face *face = (FT_Face *)fontface->faces[0];
                        for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
                        {
                            if (fontface->faces[i] != 0)
                            {
                                FT_UInt glyph_index = FT_Get_Char_Index(*((FT_Face *)fontface->faces[i]), *text);
                                if (glyph_index != 0)
                                {
                                    // This font has this glyph. Use this instead of the original.
                                    face = (FT_Face *)fontface->faces[i];
                                    break;
                                }
                            }
                        }
                        int error = FT_Load_Char(*face, *text, FT_LOAD_RENDER);
                        if (error)
                        {
                            return error;
                        }

                        // Copy it out onto our buffer.
                        FT_GlyphSlot slot = (*face)->glyph;

                        if (draw)
                        {
                            // Alpha-composite the grayscale bitmap, treating it as an
                            // alpha map.
                            __draw_bitmap(
                                tx + slot->bitmap_left,
                                ty + (lineheight - slot->bitmap_top),
                                slot->bitmap.width,
                                slot->bitmap.rows,
                                slot->bitmap.pixel_mode,
                                slot->bitmap.buffer,
                                color
                            );
                        }

                        // Add it to the cache so we can render faster next time.
                        if (fontface->cacheloc < fontface->cachesize)
                        {
                            entry = __cache_create(
                                *text,
                                slot->advance.x >> 6,
                                slot->advance.y >> 6,
                                slot->bitmap_left,
                                slot->bitmap_top,
                                slot->bitmap.width,
                                slot->bitmap.rows,
                                slot->bitmap.pixel_mode,
                                slot->bitmap.buffer
                             );
                            __cache_add(fontface, entry);
                        }

                        // Advance the pen based on this glyph.
                        tx += (slot->advance.x >> 6);
                        ty += (slot->advance.y >> 6);
                    }
                    if (metrics)
                    {
                        metrics->width = metrics->width > tx ? metrics->width : tx;
                        metrics->height = ty + lineheight;
                    }
                    break;
                }
            }

            text++;
        }

        free(freeptr);
        return 0;
    }
    else
    {
        return -1;
    }
}

int video_draw_character(int x, int y, font_t *fontface, uint32_t color, int ch)
{
    return __video_draw_calc_character(x, y, fontface, color, ch, 0, 1);
}

int video_draw_text(int x, int y, font_t *fontface, uint32_t color, const char * const msg, ...)
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
            return __video_draw_calc_text(x, y, fontface, color, buffer, 0, 1);
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

font_metrics_t video_get_character_metrics(font_t *fontface, int ch)
{
    font_metrics_t metrics;
    
    if (__video_draw_calc_character(0, 0, fontface, 0, ch, &metrics, 0) == 0)
    {
        return metrics;
    }
    else
    {
        metrics.width = 0;
        metrics.height = 0;
        return metrics;
    }
}

font_metrics_t video_get_text_metrics(font_t *fontface, const char * const msg, ...)
{
    if (msg)
    {
        char buffer[2048];
        va_list args;
        va_start(args, msg);
        int length = vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        font_metrics_t metrics;
        if (length > 0)
        {
            buffer[min(length, 2047)] = 0;
            if (__video_draw_calc_text(0, 0, fontface, 0, buffer, &metrics, 0) == 0)
            {
                return metrics;
            }
        }

        metrics.width = 0;
        metrics.height = 0;
        return metrics;
    }
    else
    {
        font_metrics_t metrics;
        metrics.width = 0;
        metrics.height = 0;
        return metrics;
    }

}
#endif
