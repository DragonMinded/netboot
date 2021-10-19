#include <stdint.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "naomi/system.h"
#include "naomi/video.h"

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
    // by index and then did a binary search.

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
    }
    else
    {
        return -1;
    }
}

void __draw_bitmap(int x, int y, unsigned int width, unsigned int height, unsigned int mode, uint8_t *buffer, uint32_t color)
{
    // Grab the color itself.
    unsigned int sr;
    unsigned int sg;
    unsigned int sb;
    explodergb(color, &sr, &sg, &sb);

    if (mode == FT_PIXEL_MODE_GRAY)
    {
        for (int yp = 0; yp < height; yp++)
        {
            unsigned int row = yp * width;
            for(int xp = 0; xp < width; xp++)
            {
                // Alpha-blend the grayscale image with the destination.
                unsigned int alpha = buffer[row + xp];

                if (alpha != 0)
                {
                    if(alpha >= 255)
                    {
                        video_draw_pixel(x + xp, y + yp, color);
                    }
                    else
                    {
                        unsigned int dr;
                        unsigned int dg;
                        unsigned int db;

                        // Technically it should be divided by 255, but this should
                        // be much much faster.
                        explodergb(video_get_pixel(x + xp, y + yp), &dr, &dg, &db); 
                        dr = ((sr * alpha) + (dr * (255 - alpha))) >> 8;
                        dg = ((sg * alpha) + (dg * (255 - alpha))) >> 8;
                        db = ((sb * alpha) + (db * (255 - alpha))) >> 8;
                        video_draw_pixel(x + xp, y + yp, rgb(dr, dg, db));
                    }
                }
            }
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

                        if (tx >= video_width())
                        {
                            tx = 0;
                            ty += lineheight;
                        }
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

                        if (tx >= video_width())
                        {
                            tx = 0;
                            ty += lineheight;
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
                    }
                    break;
                }
                default:
                {
                    font_cache_entry_t *entry = __cache_lookup(fontface, *text);
                    if (entry)
                    {
                        // Make sure we wrap if we hit the end.
                        if ((tx + entry->advancex) >= video_width())
                        {
                            tx = 0;
                            ty += lineheight;
                        }

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

                        // Make sure we wrap if we hit the end.
                        if ((tx + (slot->advance.x >> 6)) >= video_width())
                        {
                            tx = 0;
                            ty += lineheight;
                        }

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

        if (buffer)
        {
            va_list args;
            va_start(args, msg);
            vsnprintf(buffer, 2047, msg, args);
            va_end(args);

            int err = __video_draw_text(x, y, fontface, color, buffer);
            return err;
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
