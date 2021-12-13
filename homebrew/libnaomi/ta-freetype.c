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

// The UVsize of a sprite sheet. Note that this must be kept in sync with the
// draw function U and V sizes below.
#define SPRITEMAP_UVSIZE 256

typedef struct
{
    void *texture;
    int u;
    int v;
} ta_cache_entry_t;

void *curtex = 0;
static int uloc = 0;
static int vloc = 0;
static int vsize = -1;

font_cache_entry_t *_ta_cache_create(uint32_t index, int advancex, int advancey, int bitmap_left, int bitmap_top, int width, int height, int mode, uint8_t *buffer)
{
    font_cache_entry_t *entry = malloc(sizeof(font_cache_entry_t));
    if (entry == 0)
    {
        return 0;
    }
    entry->index = index;
    entry->cache_namespace = FONT_CACHE_TA;
    entry->advancex = advancex;
    entry->advancey = advancey;
    entry->bitmap_left = bitmap_left;
    entry->bitmap_top = bitmap_top;
    entry->mode = mode;
    entry->width = width;
    entry->height = height;
    ta_cache_entry_t *ta_entry = malloc(sizeof(ta_cache_entry_t));
    entry->data = ta_entry;
    if (entry->data == 0)
    {
        free(entry);
        return 0;
    }

    if (width > 0 && height > 0 && mode == FT_PIXEL_MODE_GRAY)
    {
        // Let's try to find a spritemap to add this character to.
        if (curtex == 0 || ((uloc + width) >= SPRITEMAP_UVSIZE && (vloc + vsize + height) >= SPRITEMAP_UVSIZE))
        {
            // We ran out of room, we need a new spritemap.
            curtex = ta_texture_malloc(SPRITEMAP_UVSIZE, 16);
            if (curtex == 0)
            {
                free(entry->data);
                free(entry);
                return 0;
            }

            uloc = 0;
            vloc = 0;
            vsize = -1;
        }
        else if((uloc + width) >= SPRITEMAP_UVSIZE)
        {
            if (vsize == -1)
            {
                // Can't cache this character at all!
                free(entry->data);
                free(entry);
                return 0;
            }

            uloc = 0;
            vloc = vloc + vsize;
            vsize = -1;
        }

        // If we reach here, we have room in the spritesheet for this character copy it over.
        uint16_t *created_buffer = malloc(sizeof(uint16_t) * width * height);
        if (created_buffer == 0)
        {
            free(entry->data);
            free(entry);
            return 0;
        }

        // Create a 4444 pixel of pure white (so we can multiply by our actual color later)
        // with the alpha channel set to the intensity of the buffer at that pixel.
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                created_buffer[x + (y * width)] = RGB4444(255, 255, 255, buffer[x + (y * width)]);
            }
        }

        /* Load this created sprite into the spritemap, save the cache pointer. */
        ta_texture_load_sprite(curtex, SPRITEMAP_UVSIZE, 16, uloc, vloc, width, height, created_buffer);
        free(created_buffer);

        ta_entry->texture = curtex;
        ta_entry->u = uloc;
        ta_entry->v = vloc;

        /* Adjust where the next character will be stored. */
        uloc += width;
        vsize = height > vsize ? height : vsize;
    }
    else
    {
        ta_entry->texture = 0;
        ta_entry->u = 0;
        ta_entry->v = 0;
    }

    /* We successfully created a cache for this character. */
    return entry;
}

// Forward definitions of stuff we don't want in public headers.
extern unsigned int global_video_vertical;
extern unsigned int global_video_width;
extern unsigned int cached_actual_width;
extern unsigned int cached_actual_height;
uint32_t _ta_16bit_uv(float uv);

void _ta_draw_uncached_bitmap(int x, int y, unsigned int width, unsigned int height, uint8_t *data, color_t color)
{
    // We can't draw this, since we don't have the VRAM for it. So, give up. Perhaps in the future
    // we might schedule a framebuffer fallback? Not sure.
}

void _ta_draw_cached_bitmap_horiz(int x, int y, unsigned int width, unsigned int height, void *data, color_t color)
{
    ta_cache_entry_t *ta_entry = data;
    if (ta_entry->texture)
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

        float ulow = (float)(ta_entry->u + low_x) / (float)SPRITEMAP_UVSIZE;
        float vlow = (float)(ta_entry->v + low_y) / (float)SPRITEMAP_UVSIZE;
        float uhigh = (float)(ta_entry->u + high_x) / (float)SPRITEMAP_UVSIZE;
        float vhigh = (float)(ta_entry->v + high_y) / (float)SPRITEMAP_UVSIZE;

        textured_vertex_t verticies[4] = {
            { (float)(x + low_x), (float)(y + high_y), 1.0, ulow, vhigh },
            { (float)(x + low_x), (float)(y + low_y), 1.0, ulow, vlow },
            { (float)(x + high_x), (float)(y + low_y), 1.0, uhigh, vlow },
            { (float)(x + high_x), (float)(y + high_y), 1.0, uhigh, vhigh },
        };

        // This doesn't use the sprite draw routines as it is slightly different
        // (modulates the color against an all-white sprite instead of just using
        // decal mode).
        struct polygon_list_sprite mypoly;
        struct vertex_list_sprite myvertex;

        mypoly.cmd =
            TA_CMD_SPRITE |
            TA_CMD_POLYGON_TYPE_TRANSPARENT |
            TA_CMD_POLYGON_SUBLIST |
            TA_CMD_POLYGON_PACKED_COLOR |
            TA_CMD_POLYGON_16BIT_UV |
            TA_CMD_POLYGON_TEXTURED;
        mypoly.mode1 =
            TA_POLYMODE1_Z_NEVER |
            TA_POLYMODE1_CULL_DISABLED;
        mypoly.mode2 =
            TA_POLYMODE2_MIPMAP_D_1_00 |
            TA_POLYMODE2_TEXTURE_MODULATE |
            TA_POLYMODE2_U_SIZE_256 |
            TA_POLYMODE2_V_SIZE_256 |
            TA_POLYMODE2_TEXTURE_CLAMP_U |
            TA_POLYMODE2_TEXTURE_CLAMP_V |
            TA_POLYMODE2_FOG_DISABLED |
            TA_POLYMODE2_SRC_BLEND_SRC_ALPHA |
            TA_POLYMODE2_DST_BLEND_INV_SRC_ALPHA;
        mypoly.texture =
            TA_TEXTUREMODE_ARGB4444 |
            TA_TEXTUREMODE_ADDRESS(ta_entry->texture);
        mypoly.mult_color = RGB0888(color.r, color.g, color.b);
        mypoly.add_color = 0;
        ta_commit_list(&mypoly, TA_LIST_SHORT);

        myvertex.cmd = TA_CMD_VERTEX | TA_CMD_VERTEX_END_OF_STRIP;
        myvertex.ax = verticies[0].x;
        myvertex.ay = verticies[0].y;
        myvertex.az = verticies[0].z;
        myvertex.bx = verticies[1].x;
        myvertex.by = verticies[1].y;
        myvertex.bz = verticies[1].z;
        myvertex.cx = verticies[2].x;
        myvertex.cy = verticies[2].y;
        myvertex.cz = verticies[2].z;
        myvertex.dx = verticies[3].x;
        myvertex.dy = verticies[3].y;
        myvertex.au_av = (_ta_16bit_uv(verticies[0].u) << 16) | _ta_16bit_uv(verticies[0].v);
        myvertex.bu_bv = (_ta_16bit_uv(verticies[1].u) << 16) | _ta_16bit_uv(verticies[1].v);
        myvertex.cu_cv = (_ta_16bit_uv(verticies[2].u) << 16) | _ta_16bit_uv(verticies[2].v);
        ta_commit_list(&myvertex, TA_LIST_LONG);
    }
}

void _ta_draw_cached_bitmap_vert(int x, int y, unsigned int width, unsigned int height, void *data, color_t color)
{
    ta_cache_entry_t *ta_entry = data;
    if (ta_entry->texture)
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

        float ulow = (float)(ta_entry->u + low_x) / (float)SPRITEMAP_UVSIZE;
        float vlow = (float)(ta_entry->v + low_y) / (float)SPRITEMAP_UVSIZE;
        float uhigh = (float)(ta_entry->u + high_x) / (float)SPRITEMAP_UVSIZE;
        float vhigh = (float)(ta_entry->v + high_y) / (float)SPRITEMAP_UVSIZE;

        textured_vertex_t verticies[4] = {
            { (float)(x + low_x), (float)(y + high_y), 1.0, ulow, vhigh },
            { (float)(x + low_x), (float)(y + low_y), 1.0, ulow, vlow },
            { (float)(x + high_x), (float)(y + low_y), 1.0, uhigh, vlow },
            { (float)(x + high_x), (float)(y + high_y), 1.0, uhigh, vhigh },
        };

        // This doesn't use the sprite draw routines as it is slightly different
        // (modulates the color against an all-white sprite instead of just using
        // decal mode).
        struct polygon_list_sprite mypoly;
        struct vertex_list_sprite myvertex;

        mypoly.cmd =
            TA_CMD_SPRITE |
            TA_CMD_POLYGON_TYPE_TRANSPARENT |
            TA_CMD_POLYGON_SUBLIST |
            TA_CMD_POLYGON_PACKED_COLOR |
            TA_CMD_POLYGON_16BIT_UV |
            TA_CMD_POLYGON_TEXTURED;
        mypoly.mode1 =
            TA_POLYMODE1_Z_NEVER |
            TA_POLYMODE1_CULL_DISABLED;
        mypoly.mode2 =
            TA_POLYMODE2_MIPMAP_D_1_00 |
            TA_POLYMODE2_TEXTURE_MODULATE |
            TA_POLYMODE2_U_SIZE_256 |
            TA_POLYMODE2_V_SIZE_256 |
            TA_POLYMODE2_TEXTURE_CLAMP_U |
            TA_POLYMODE2_TEXTURE_CLAMP_V |
            TA_POLYMODE2_FOG_DISABLED |
            TA_POLYMODE2_SRC_BLEND_SRC_ALPHA |
            TA_POLYMODE2_DST_BLEND_INV_SRC_ALPHA;
        mypoly.texture =
            TA_TEXTUREMODE_ARGB4444 |
            TA_TEXTUREMODE_ADDRESS(ta_entry->texture);
        mypoly.mult_color = RGB0888(color.r, color.g, color.b);
        mypoly.add_color = 0;
        ta_commit_list(&mypoly, TA_LIST_SHORT);

        myvertex.cmd = TA_CMD_VERTEX | TA_CMD_VERTEX_END_OF_STRIP;
        float vwidth = (float)global_video_width - 1.0;
        myvertex.ax = vwidth - verticies[0].y;
        myvertex.ay = verticies[0].x;
        myvertex.az = verticies[0].z;
        myvertex.bx = vwidth - verticies[1].y;
        myvertex.by = verticies[1].x;
        myvertex.bz = verticies[1].z;
        myvertex.cx = vwidth - verticies[2].y;
        myvertex.cy = verticies[2].x;
        myvertex.cz = verticies[2].z;
        myvertex.dx = vwidth - verticies[3].y;
        myvertex.dy = verticies[3].x;
        myvertex.au_av = (_ta_16bit_uv(verticies[0].u) << 16) | _ta_16bit_uv(verticies[0].v);
        myvertex.bu_bv = (_ta_16bit_uv(verticies[1].u) << 16) | _ta_16bit_uv(verticies[1].v);
        myvertex.cu_cv = (_ta_16bit_uv(verticies[2].u) << 16) | _ta_16bit_uv(verticies[2].v);
        ta_commit_list(&myvertex, TA_LIST_LONG);
    }
}

int ta_draw_character(int x, int y, font_t *fontface, color_t color, int ch)
{
    return _font_draw_calc_character(
        x,
        y,
        fontface,
        color,
        ch,
        0,
        &_ta_cache_create,
        FONT_CACHE_TA,
        &_ta_draw_uncached_bitmap,
        global_video_vertical ? &_ta_draw_cached_bitmap_vert : &_ta_draw_cached_bitmap_horiz
    );
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
            return _font_draw_calc_text(
                x,
                y,
                fontface,
                color,
                buffer,
                0,
                &_ta_cache_create,
                FONT_CACHE_TA,
                &_ta_draw_uncached_bitmap,
                global_video_vertical ? &_ta_draw_cached_bitmap_vert : &_ta_draw_cached_bitmap_horiz
            );
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
