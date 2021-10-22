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
    font->face = malloc(sizeof(FT_Face));

    if (FT_New_Memory_Face(*library, buffer, size, 0, (FT_Face *)font->face))
    {
        free(font->face);
        free(font);
        return 0;
    }

    font->cachesize = FONT_CACHE_SIZE;
    font->cacheloc = 0;
    font->cache = malloc(sizeof(font_cache_entry_t *) * font->cachesize);
    memset(font->cache, 0, sizeof(font_cache_entry_t *) * font->cachesize);

    video_font_set_size(font, 12);

    return font;
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
        FT_Done_Face(*((FT_Face *)fontface->face));
        __cache_discard(fontface);
        free(fontface->cache);
        free(fontface);
    }
}

int video_font_set_size(font_t *fontface, unsigned int size)
{
    if (fontface)
    {
        int error = FT_Set_Pixel_Sizes(*((FT_Face *)fontface->face), 0, size);
        if (error)
        {
            return error;
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
        unsigned int low_x = 0;
        unsigned int high_x = width;
        unsigned int low_y = 0;
        unsigned int high_y = height;

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
                for (int yp = low_y; yp < high_y; yp++)
                {
                    for(int xp = low_x; xp < high_x; xp++)
                    {
                        // Alpha-blend the grayscale image with the destination.
                        // We only support 32 alpha levels here for speed.
                        uint8_t alpha = buffer[(yp * width) + xp] | 0x7;

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

                                // Technically it should be divided by 255, but this should
                                // be much much faster for an 0.4% accuracy loss.
                                EXPLODE0555(GET_PIXEL_V_2(buffer_base, x + xp, y + yp), dr, dg, db);
                                dr = ((sr * alpha) + (dr * (~alpha))) >> 8;
                                dg = ((sg * alpha) + (dg * (~alpha))) >> 8;
                                db = ((sb * alpha) + (db * (~alpha))) >> 8;
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
                        uint8_t alpha = buffer[(yp * width) + xp] | 0x7;

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

                                // Technically it should be divided by 255, but this should
                                // be much much faster for an 0.4% accuracy loss.
                                EXPLODE0555(GET_PIXEL_H_2(buffer_base, x + xp, y + yp), dr, dg, db);
                                dr = ((sr * alpha) + (dr * (~alpha))) >> 8;
                                dg = ((sg * alpha) + (dg * (~alpha))) >> 8;
                                db = ((sb * alpha) + (db * (~alpha))) >> 8;
                                SET_PIXEL_H_2(buffer_base, x + xp, y + yp, RGB0555(dr, dg, db));
                            }
                        }
                    }
                }
            }
        }
        else
        {
            // TODO
        }
    }
    else
    {
        // TODO: Need to support bitmapped fonts.
    }
}

int video_draw_character(int x, int y, font_t *fontface, uint32_t color, int ch)
{
    if (fontface)
    {
        font_cache_entry_t *entry = __cache_lookup(fontface, ch);
        unsigned int lineheight = fontface->lineheight;

        if (entry)
        {
            x += entry->bitmap_left;
            y += lineheight - entry->bitmap_top;
            __draw_bitmap(x, y, entry->width, entry->height, entry->mode, entry->buffer, color);
        }
        else
        {
            // Grab the actual unicode glyph.
            FT_Face *face = (FT_Face *)fontface->face;
            int error = FT_Load_Char(*face, ch, FT_LOAD_RENDER);
            if (error)
            {
                return error;
            }

            // Copy it out onto our buffer.
            FT_GlyphSlot slot = (*face)->glyph;
            x += slot->bitmap_left;
            y += lineheight - slot->bitmap_top;

            // Alpha-composite the grayscale bitmap, treating it as an
            // alpha map.
            __draw_bitmap(x, y, slot->bitmap.width, slot->bitmap.rows, slot->bitmap.pixel_mode, slot->bitmap.buffer, color);

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

int __video_draw_text( int x, int y, font_t *fontface, uint32_t color, const char * const msg )
{
    if( msg == 0 ) { return 0; }

    int tx = x;
    int ty = y;
    uint32_t *text = utf8_convert(msg);

    if (text)
    {
        uint32_t *freeptr = text;
        FT_Face *face = (FT_Face *)fontface->face;
        unsigned int lineheight = fontface->lineheight;

        while( *text )
        {
            switch( *text )
            {
                case '\r':
                case '\n':
                {
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
                    break;
                }
                default:
                {
                    font_cache_entry_t *entry = __cache_lookup(fontface, *text);
                    if (entry)
                    {
                        __draw_bitmap(tx + entry->bitmap_left, ty + lineheight - entry->bitmap_top, entry->width, entry->height, entry->mode, entry->buffer, color);

                        // Advance the pen based on this glyph.
                        tx += entry->advancex;
                        ty += entry->advancey;
                    }
                    else
                    {
                        int error = FT_Load_Char(*face, *text, FT_LOAD_RENDER);
                        if (error == FT_Err_Invalid_Character_Code)
                        {
                            error = FT_Load_Char(*face, '?', FT_LOAD_RENDER);
                        }
                        if (error)
                        {
                            return error;
                        }

                        // Copy it out onto our buffer.
                        FT_GlyphSlot slot = (*face)->glyph;

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


int video_draw_text(int x, int y, font_t *fontface, uint32_t color, const char * const msg, ...)
{
    if (msg)
    {
        static char buffer[2048];
        va_list args;
        va_start(args, msg);
        vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        return __video_draw_text(x, y, fontface, color, buffer);
    }
    else
    {
        return 0;
    }
}
#endif
