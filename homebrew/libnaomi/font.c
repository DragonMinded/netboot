#ifdef FEATURE_FREETYPE
// Only build this stuff if freetype is installed. Otherwise just don't do anything with it.
// This is so that stage 1 libnaomi.a can be built, and then freetype built against it, before
// libnaomi is built again.
#include <stdint.h>
#include <stdarg.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "naomi/utf8.h"
#include "naomi/font.h"
#include "font-internal.h"
#include "irqinternal.h"

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

FT_Library * __freetype_init()
{
    static FT_Library library;
    static int init = 0;

    if (!init)
    {
        FT_Init_FreeType(&library);
    }

    return &library;
}

font_t * font_add(void *buffer, unsigned int size)
{
    FT_Library *library = __freetype_init();
    font_t *font = malloc(sizeof(font_t));
    if (font == 0)
    {
        return 0;
    }
    font->faces = malloc(sizeof(void *) * MAX_FALLBACK_SIZE);
    if (font->faces == 0)
    {
        free(font);
        return 0;
    }
    memset(font->faces, 0, sizeof(void *) * MAX_FALLBACK_SIZE);
    font->faces[0] = malloc(sizeof(FT_Face));
    if (font->faces[0] == 0)
    {
        free(font->faces);
        free(font);
        return 0;
    }

    font->cachesize = FONT_CACHE_SIZE;
    font->cacheloc = 0;
    font->cache = malloc(sizeof(void *) * font->cachesize);
    if (font->cache == 0)
    {
        free(font->faces[0]);
        free(font->faces);
        free(font);
        return 0;
    }
    memset(font->cache, 0, sizeof(void *) * font->cachesize);

    if (FT_New_Memory_Face(*library, buffer, size, 0, (FT_Face *)font->faces[0]))
    {
        free(font->cache);
        free(font->faces[0]);
        free(font->faces);
        free(font);
        return 0;
    }
    FT_Select_Charmap(*((FT_Face *)font->faces[0]), FT_ENCODING_UNICODE);

    font_set_size(font, 12);

    return font;
}

int font_add_fallback(font_t *font, void *buffer, unsigned int size)
{
    for (int i = 0; i < MAX_FALLBACK_SIZE; i++)
    {
        if (font->faces[i] == 0)
        {
            FT_Library *library = __freetype_init();
            font->faces[i] = malloc(sizeof(FT_Face));
            if (font->faces[i] == 0)
            {
                return -1;
            }
            int error = FT_New_Memory_Face(*library, buffer, size, 0, (FT_Face *)font->faces[i]);
            if (error)
            {
                free(font->faces[i]);
                font->faces[i] = 0;
                return error;
            }
            FT_Select_Charmap(*((FT_Face *)font->faces[i]), FT_ENCODING_UNICODE);
            font_set_size(font, font->lineheight);

            return 0;
        }
    }

    return -1;
}

void _font_cache_discard(font_t *fontface)
{
    for (int i = 0; i < fontface->cacheloc; i++)
    {
        free(((font_cache_entry_t *)fontface->cache[i])->data);
        free(fontface->cache[i]);
        fontface->cache[i] = 0;
    }

    fontface->cacheloc = 0;
}

font_cache_entry_t *_font_cache_lookup(font_t *fontface, int cache_namespace, uint32_t index)
{
    // This is linear and we could make it a lot better if we sorted by index
    // and then did a binary search. The lion's share of this module's compute
    // time goes to drawing glyphs to the screen however, so I didn't bother.
    for (int i = 0; i < fontface->cacheloc; i++)
    {
        font_cache_entry_t *entry = (font_cache_entry_t *)fontface->cache[i];

        if(entry->index == index && (cache_namespace == FONT_CACHE_ANY || entry->cache_namespace == cache_namespace))
        {
            return fontface->cache[i];
        }
    }

    return 0;
}

int _font_cache_add(font_t *fontface, void *entry)
{
    if (fontface->cacheloc == fontface->cachesize)
    {
        return 0;
    }
    if (entry == 0)
    {
        return 0;
    }

    fontface->cache[fontface->cacheloc++] = entry;
    return 1;
}

void font_discard(font_t *fontface)
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
        _font_cache_discard(fontface);
        free(fontface->cache);
        free(fontface->faces);
        free(fontface);
    }
}

int font_set_size(font_t *fontface, unsigned int size)
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
        _font_cache_discard(fontface);

        return 0;
    }
    else
    {
        return -1;
    }
}

int _font_draw_calc_character(                                                                                                                                                                   int x,
    int y,
    font_t *fontface,
    color_t color,
    int ch,
    font_metrics_t *metrics,
    cache_func_t cache_func,
    int cache_namespace,
    uncached_draw_func_t uncached_draw,
    cached_draw_func_t cached_draw
) {
    if(cache_namespace == FONT_CACHE_ANY && (uncached_draw != 0 || cached_draw != 0))
    {
        _irq_display_invariant("font failure", "cannot render text with wildcard font cache namespace!");
    }

    if (fontface)
    {
        font_cache_entry_t *entry = _font_cache_lookup(fontface, cache_namespace, ch);
        unsigned int lineheight = fontface->lineheight;

        if (entry)
        {
            x += entry->bitmap_left;
            y += lineheight - entry->bitmap_top;

            if (cached_draw && entry->mode == FT_PIXEL_MODE_GRAY)
            {
                cached_draw(x, y, entry->width, entry->height, entry->data, color);
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

            // Add it to the cache so we can render faster next time.
            if (cache_func && fontface->cacheloc < fontface->cachesize)
            {
                entry = cache_func(
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
                _font_cache_add(fontface, entry);
                if (cached_draw && entry->mode == FT_PIXEL_MODE_GRAY)
                {
                    cached_draw(x, y, entry->width, entry->height, entry->data, color);
                }
            }
            else if (uncached_draw && slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
            {
                // Alpha-composite the grayscale bitmap, treating it as an alpha map.
                uncached_draw(x, y, slot->bitmap.width, slot->bitmap.rows, slot->bitmap.buffer, color);
            }

            if (metrics)
            {
                metrics->width = slot->advance.x >> 6;
                metrics->height = lineheight;
            }
        }

        return 0;
    }
    else
    {
        return -1;
    }
}

int _font_draw_calc_text(
    int x,
    int y,
    font_t *fontface,
    color_t color,
    const char * const msg,
    font_metrics_t *metrics,
    cache_func_t cache_func,
    int cache_namespace,
    uncached_draw_func_t uncached_draw,
    cached_draw_func_t cached_draw
) {
    if (metrics)
    {
        metrics->width = 0;
        metrics->height = 0;
    }
    if( msg == 0 ) { return 0; }

    if(cache_namespace == FONT_CACHE_ANY && (uncached_draw != 0 || cached_draw != 0))
    {
        _irq_display_invariant("font failure", "cannot render text with wildcard font cache namespace!");
    }

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
                    font_cache_entry_t *entry = _font_cache_lookup(fontface, cache_namespace, *text);
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
                        if (cache_func && fontface->cacheloc < fontface->cachesize)
                        {
                            entry = cache_func(
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
                            _font_cache_add(fontface, entry);
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
                    font_cache_entry_t *entry = _font_cache_lookup(fontface, cache_namespace, *text);
                    if (entry)
                    {
                        if (cached_draw && entry->mode == FT_PIXEL_MODE_GRAY)
                        {
                            cached_draw(tx + entry->bitmap_left, ty + lineheight - entry->bitmap_top, entry->width, entry->height, entry->data, color);
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

                        // Add it to the cache so we can render faster next time.
                        if (cache_func && fontface->cacheloc < fontface->cachesize)
                        {
                            entry = cache_func(
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
                            _font_cache_add(fontface, entry);
                            if (cached_draw && entry->mode == FT_PIXEL_MODE_GRAY)
                            {
                                cached_draw(tx + entry->bitmap_left, ty + lineheight - entry->bitmap_top, entry->width, entry->height, entry->data, color);
                            }
                        }
                        else if (uncached_draw && slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
                        {
                            // Alpha-composite the grayscale bitmap, treating it as an
                            // alpha map.
                            uncached_draw(
                                tx + slot->bitmap_left,
                                ty + (lineheight - slot->bitmap_top),
                                slot->bitmap.width,
                                slot->bitmap.rows,
                                slot->bitmap.buffer,
                                color
                            );
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

font_metrics_t font_get_character_metrics(font_t *fontface, int ch)
{
    font_metrics_t metrics;
    
    if (_font_draw_calc_character(0, 0, fontface, rgb(0, 0, 0), ch, &metrics, 0, FONT_CACHE_ANY, 0, 0) == 0)
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

font_metrics_t font_get_text_metrics(font_t *fontface, const char * const msg, ...)
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
            if (_font_draw_calc_text(0, 0, fontface, rgb(0, 0, 0), buffer, &metrics, 0, FONT_CACHE_ANY, 0, 0) == 0)
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
