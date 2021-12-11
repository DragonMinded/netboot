#ifndef __TA_H
#define __TA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <malloc.h>
#include "naomi/video.h"
#include "naomi/matrix.h"

/* Command: User clip */
struct user_clip_list
{
    unsigned int cmd;
    int not_used[3];
    float xmin;
    float ymin;
    float xmax;
    float ymax;
};

/*
 * Command: Polygon / Modifier volume
 *
 * Usable with packed color textured and untextured polygons.
 */
struct polygon_list_packed_color
{
    unsigned int cmd;
    unsigned int mode1;
    unsigned int mode2;
    /* When using untextured polygons, the texture word is left at 0 */
    unsigned int texture;
    int not_used[4];
};

/*
 * Command: Polygon / Modifier volume
 *
 * Usable with intensity color textured and untextured polygons.
 */
struct polygon_list_intensity
{
    unsigned int cmd;
    unsigned int mode1;
    unsigned int mode2;
    /* When using untextured polygons, the texture word is left at 0 */
    unsigned int texture;
    float face_alpha;
    float face_red;
    float face_green;
    float face_blue;
};
/*
 * Command: Polygon / Modifier volume
 *
 * Usable with textured and untextured sprites (quads with no perspective).
 */
struct polygon_list_sprite
{
    unsigned int cmd;
    unsigned int mode1;
    unsigned int mode2;
    /* When using untextured sprites, the texture word is left at 0 */
    unsigned int texture;
    unsigned int mult_color;
    /* When using untextured sprites, the additive color is ignored */
    unsigned int add_color;
    int not_used[2];
};


/* Command: Modifier list */
struct modifier_list
{
    unsigned int cmd;
    unsigned int instruction;
    int not_used[6];
};

/*
 * Command: Vertex
 *
 * Usable with packed color polygons as well as intensity polygons.
 */
struct vertex_list_packed_color_32bit_uv
{
    unsigned int cmd;
    float x;
    float y;
    float z;
    float u;
    float v;
    /* Depending on the color mode, neither, just base_color or both base_color
     * and offset_color will be used. */
    unsigned int mult_color;
    unsigned int add_color;
};

/*
 * Command: Vertex
 *
 * Usable with both textured and untextured sprites.
 */
struct vertex_list_sprite
{
    unsigned int cmd;
    float ax;
    float ay;
    float az;
    float bx;
    float by;
    float bz;
    float cx;
    float cy;
    float cz;
    float dx;
    float dy;
    /* Only used for textured sprites. For untextured,
     * these four are ignored. */
    int not_used;
    unsigned int au_av;
    unsigned int bu_bv;
    unsigned int cu_cv;
};

// Defines for the high byte of the TA cmd.
#define TA_CMD_END_OF_LIST                0x00000000
#define TA_CMD_USER_TILE_CLIP             0x20000000
#define TA_CMD_OBJECT_LIST_SET            0x40000000
#define TA_CMD_POLYGON                    0x80000000
#define TA_CMD_SPRITE                     0xA0000000
#define TA_CMD_VERTEX                     0xE0000000

#define TA_CMD_VERTEX_END_OF_STRIP        0x10000000

#define TA_CMD_POLYGON_TYPE_OPAQUE        0x00000000
#define TA_CMD_MODIFIER_TYPE_OPAQUE       0x01000000
#define TA_CMD_POLYGON_TYPE_TRANSPARENT   0x02000000
#define TA_CMD_MODIFIER_TYPE_TRANSPARENT  0x03000000
#define TA_CMD_POLYGON_TYPE_PUNCHTHRU     0x04000000

// Defines for the next byte of the TA cmd.
#define TA_CMD_POLYGON_SUBLIST            0x00800000
#define TA_CMD_POLYGON_STRIPLENGTH_1      (0<<18)
#define TA_CMD_POLYGON_STRIPLENGTH_2      (1<<18)
#define TA_CMD_POLYGON_STRIPLENGTH_4      (2<<18)
#define TA_CMD_POLYGON_STRIPLENGTH_6      (3<<18)
#define TA_CMD_POLYGON_USER_CLIP_INSIDE   0x00020000
#define TA_CMD_POLYGON_USER_CLIP_OUTSIDE  0x00030000

// Defines for the bottom two bytes of the TA cmd.
#define TA_CMD_POLYGON_SHADOW_MODIFIER    0x00000080
#define TA_CMD_POLYGON_VOLUME_MODIFIER    0x00000040
#define TA_CMD_POLYGON_PACKED_COLOR       (0<<4)
#define TA_CMD_POLYGON_FLOAT_COLOR        (1<<4)
#define TA_CMD_POLYGON_INTENSITY          (2<<4)
#define TA_CMD_POLYGON_PREVFACE_INTENSITY (3<<4)
#define TA_CMD_POLYGON_TEXTURED           0x00000008
#define TA_CMD_POLYGON_SPECULAR_HIGHLIGHT 0x00000004
#define TA_CMD_POLYGON_GOURAUD_SHADING    0x00000002
#define TA_CMD_POLYGON_16BIT_UV           0x00000001

// Defines for the mode1 word of the TA cmd.
#define TA_POLYMODE1_Z_NEVER        (0<<29)
#define TA_POLYMODE1_Z_LESS         (1<<29)
#define TA_POLYMODE1_Z_EQUAL        (2<<29)
#define TA_POLYMODE1_Z_LESSEQUAL    (3<<29)
#define TA_POLYMODE1_Z_GREATER      (4<<29)
#define TA_POLYMODE1_Z_NOTEQUAL     (5<<29)
#define TA_POLYMODE1_Z_GREATEREQUAL (6<<29)
#define TA_POLYMODE1_Z_ALWAYS       (7<<29)
#define TA_POLYMODE1_CULL_DISABLED  (0<<27)
#define TA_POLYMODE1_CULL_SMALL     (1<<27)
#define TA_POLYMODE1_CULL_CCW       (2<<27)
#define TA_POLYMODE1_CULL_CW        (3<<27)
#define TA_POLYMODE1_NO_Z_UPDATE    0x04000000
#define TA_POLYMODE1_TEXTURED       0x02000000
#define TA_POLYMODE1_OFFSET_COLOR   0x01000000
#define TA_POLYMODE1_GOURAD_SHADED  0x00800000
#define TA_POLYMODE1_16BIT_UV       0x00400000
#define TA_POLYMODE1_CACHE_BYPASS   0x00200000
#define TA_POLYMODE1_DCALC_EXACT    0x00100000

// Defines for the mode2 word of the TA cmd.
#define TA_POLYMODE2_SRC_BLEND_ZERO          (0 << 29)
#define TA_POLYMODE2_SRC_BLEND_ONE           (1 << 29)
#define TA_POLYMODE2_SRC_BLEND_DST_COLOR     (2 << 29)
#define TA_POLYMODE2_SRC_BLEND_INV_DST_COLOR (3 << 29)
#define TA_POLYMODE2_SRC_BLEND_SRC_ALPHA     (4 << 29)
#define TA_POLYMODE2_SRC_BLEND_INV_SRC_ALPHA (5 << 29)
#define TA_POLYMODE2_SRC_BLEND_DST_ALPHA     (6 << 29)
#define TA_POLYMODE2_SRC_BLEND_INV_DST_ALPHA (7 << 29)

#define TA_POLYMODE2_DST_BLEND_ZERO          (0 << 26)
#define TA_POLYMODE2_DST_BLEND_ONE           (1 << 26)
#define TA_POLYMODE2_DST_BLEND_SRC_COLOR     (2 << 26)
#define TA_POLYMODE2_DST_BLEND_INV_SRC_COLOR (3 << 26)
#define TA_POLYMODE2_DST_BLEND_SRC_ALPHA     (4 << 26)
#define TA_POLYMODE2_DST_BLEND_INV_SRC_ALPHA (5 << 26)
#define TA_POLYMODE2_DST_BLEND_DST_ALPHA     (6 << 26)
#define TA_POLYMODE2_DST_BLEND_INV_DST_ALPHA (7 << 26)

#define TA_POLYMODE2_ENABLE_SRC_SECONDARY_BUFFER 0x02000000
#define TA_POLYMODE2_ENABLE_DST_SECONDARY_BUFFER 0x01000000

#define TA_POLYMODE2_FOG_TABLE      (0<<22)
#define TA_POLYMODE2_FOG_VERTEX     (1<<22)
#define TA_POLYMODE2_FOG_DISABLED   (2<<22)
#define TA_POLYMODE2_FOG_TABLE2     (3<<22)

#define TA_POLYMODE2_CLAMP_COLORS      0x00200000
#define TA_POLYMODE2_ENABLE_ALPHA      0x00100000
#define TA_POLYMODE2_DISABLE_TEX_ALPHA 0x00080000

#define TA_POLYMODE2_TEXTURE_FLIP_U   0x00040000
#define TA_POLYMODE2_TEXTURE_FLIP_V   0x00020000
#define TA_POLYMODE2_TEXTURE_CLAMP_U  0x00010000
#define TA_POLYMODE2_TEXTURE_CLAMP_V  0x00008000

#define TA_POLYMODE2_BILINEAR_FILTER    0x00002000
#define TA_POLYMODE2_TRILINEAR_A_FILTER 0x00004000
#define TA_POLYMODE2_TRILINEAR_B_FILTER 0x00006000

#define TA_POLYMODE2_ENABLE_FILTER    0x00001000

#define TA_POLYMODE2_MIPMAP_D_0_25    (1<<8)
#define TA_POLYMODE2_MIPMAP_D_0_50    (2<<8)
#define TA_POLYMODE2_MIPMAP_D_0_75    (3<<8)
#define TA_POLYMODE2_MIPMAP_D_1_00    (4<<8)
#define TA_POLYMODE2_MIPMAP_D_1_25    (5<<8)
#define TA_POLYMODE2_MIPMAP_D_1_50    (6<<8)
#define TA_POLYMODE2_MIPMAP_D_1_75    (7<<8)
#define TA_POLYMODE2_MIPMAP_D_2_00    (8<<8)
#define TA_POLYMODE2_MIPMAP_D_2_25    (9<<8)
#define TA_POLYMODE2_MIPMAP_D_2_50    (10<<8)
#define TA_POLYMODE2_MIPMAP_D_2_75    (11<<8)
#define TA_POLYMODE2_MIPMAP_D_3_00    (12<<8)
#define TA_POLYMODE2_MIPMAP_D_3_25    (13<<8)
#define TA_POLYMODE2_MIPMAP_D_3_50    (14<<8)
#define TA_POLYMODE2_MIPMAP_D_3_75    (15<<8)

#define TA_POLYMODE2_TEXTURE_DECAL          (0<<6)
#define TA_POLYMODE2_TEXTURE_MODULATE       (1<<6)
#define TA_POLYMODE2_TEXTURE_DECAL_ALPHA    (2<<6)
#define TA_POLYMODE2_TEXTURE_MODULATE_ALPHA (3<<6)

#define TA_POLYMODE2_U_SIZE_8         (0<<3)
#define TA_POLYMODE2_U_SIZE_16        (1<<3)
#define TA_POLYMODE2_U_SIZE_32        (2<<3)
#define TA_POLYMODE2_U_SIZE_64        (3<<3)
#define TA_POLYMODE2_U_SIZE_128       (4<<3)
#define TA_POLYMODE2_U_SIZE_256       (5<<3)
#define TA_POLYMODE2_U_SIZE_512       (6<<3)
#define TA_POLYMODE2_U_SIZE_1024      (7<<3)

#define TA_POLYMODE2_V_SIZE_8         (0<<0)
#define TA_POLYMODE2_V_SIZE_16        (1<<0)
#define TA_POLYMODE2_V_SIZE_32        (2<<0)
#define TA_POLYMODE2_V_SIZE_64        (3<<0)
#define TA_POLYMODE2_V_SIZE_128       (4<<0)
#define TA_POLYMODE2_V_SIZE_256       (5<<0)
#define TA_POLYMODE2_V_SIZE_512       (6<<0)
#define TA_POLYMODE2_V_SIZE_1024      (7<<0)

/* Defines for the texture word of the TA cmd. Only applies if
 * TA_CMD_POLYGON_TEXTURED is set on the cmd word. */
#define TA_TEXTUREMODE_MIPMAP         0x80000000
#define TA_TEXTUREMODE_VQ_COMPRESSION 0x40000000
#define TA_TEXTUREMODE_ARGB1555       (0<<27)
#define TA_TEXTUREMODE_RGB565         (1<<27)
#define TA_TEXTUREMODE_ARGB4444       (2<<27)
#define TA_TEXTUREMODE_YUV422         (3<<27)
#define TA_TEXTUREMODE_BUMPMAP        (4<<27)
#define TA_TEXTUREMODE_CLUT4          (5<<27)
#define TA_TEXTUREMODE_CLUT8          (6<<27)
#define TA_TEXTUREMODE_CLUTBANK8(n)   (((n) & 0x3) << 25)  /* 0-3  */
#define TA_TEXTUREMODE_CLUTBANK4(n)   (((n) & 0x3F) << 21) /* 0-63 */
#define TA_TEXTUREMODE_NON_TWIDDLED   0x04000000
#define TA_TEXTUREMODE_STRIDE         0x02000000
#define TA_TEXTUREMODE_ADDRESS(a)     ((((unsigned long)(void*)(a)) >> 3) & 0x1FFFFF)

// Function for requesting that a set of commands be rendered to the current framebuffer.
// This is just a convenience function that calls ta_render_begin() and then ta_render_wait().
// Note that if you call this with threads enabled, your thread will be parked and other
// threads can run until the TA is done. When it is done, your thread will be woken up with
// critical priority. If you run this with threads disabled, it will instead spinloop until
// the TA is done and no other work can get done.
void ta_render();

// Function for starting rendering, call this if you want to do other things while waiting
// for the render to finish. If you call this function with threads disabled, you should also
// leave them disabled until after you exit the ta_render_wait() call below.
void ta_render_begin();

// Function for finishing rendering and waiting for it to be done. If you call this with
// threads disabled, instead of parking the thread until the TA is done it will spinloop.
void ta_render_wait();

// Functions for sending list data to the TA to be rendered upon calling ta_render().
// Note that you should send something to the TA using these functions before calling ta_render()
// as these set up the TA to be able to render a frame.
void ta_commit_begin();
void ta_commit_list(void *list, int size);
void ta_commit_end();

// Set the background color for TA renders, specifically for areas where there is not any
// polygon to draw.
void ta_set_background_color(color_t color);

// Defines for the size of a ta_commit_list() call.
#define TA_LIST_SHORT 32
#define TA_LIST_LONG 64

// Given a particular palette lookup size and a bank, return the palette RAM pointer.
#define TA_PALETTE_NONE 0
#define TA_PALETTE_CLUT4 1
#define TA_PALETTE_CLUT8 2

void *ta_palette_bank(int size, int banknum);

// Given an RGBA value, return a packed color suitable for palettes.
uint32_t ta_palette_entry(color_t color);

// Get a pointer to the base texture RAM that is safe to use.
void *ta_texture_base();

// Given a UVsize (allowed sizes are 8, 16, 32, 64, 128, 256, 512 or 1024), allocate space
// in the texture RAM suitable for a texture of size UVsize * UVsize. If there is not enough
// room in the texture RAM, returns a null pointer. Note that the returned pointer is in
// texture RAM and as such must be accessed in 16-bit increments only.
void *ta_texture_malloc(int uvsize, int bitsize);

// Free a previously allocated texture.
void ta_texture_free(void *texture);

// Get statistics about the allocations in texture memory.
struct mallinfo ta_texture_mallinfo();

// Given a raw offset into texture RAM and a texture size, load the texture into texture
// RAM in twiddled format required by several video modes. Note that the texture size is
// the size in pixels of one side. The only allowed sizes are 8, 16, 32, 64, 128, 256, 512
// and 1024.
int ta_texture_load(void *offset, int uvsize, int bitsize, void *data);

// Data type for standalone UV coordinates.
typedef struct
{
    float u;
    float v;
} uv_t;

// Data type for tracking a texture and its attributes.
typedef struct
{
    void *vram_location;
    uint32_t texture_mode;
    uint32_t uvsize;
    int vram_owned;
} texture_description_t;

// Constructor functions for the above texture_description_t datatype. For paletted
// entries, the size should be one of TA_PALETTE_CLUT4 or TA_PALETTE_CLUT8. The bank
// number should be identical to the value you give to ta_palette_bank(). For direct
// texture entries, the mode should be one of TA_TEXTUREMODE_ARGB1555,
// TA_TEXTUREMODE_RGB565 or TA_TEXTUREMODE_ARGB4444. In both cases, the offset should
// be the VRAM offset of the texture you got from a ta_texture_malloc() and filled
// with a ta_texture_load(). Also, in both cases, the uvsize is the size in pixels
// of the texture in both the U and V direction, similar to what you would give to
// a ta_texture_malloc() call.
texture_description_t *ta_texture_desc_paletted(void *offset, int uvsize, int size, int banknum);
texture_description_t *ta_texture_desc_direct(void *offset, int uvsize, uint32_t mode);

// The following functions work identical to the above functions. However, they instead
// take a data offset in main RAM, allocate a new texture using ta_texture_malloc(), then
// load the texture using ta_texture_load() and finally initialize the description.
texture_description_t *ta_texture_desc_malloc_paletted(int uvsize, void *data, int size, int banknum);
texture_description_t *ta_texture_desc_malloc_direct(int uvsize, void *data, uint32_t mode);

// Frees the memory returned by one of the above four functions. Note that if you passed
// in an offset to a previously allocated texture, you are responsible for freeing that
// texture as well using ta_texture_free(). It is only done for you in functions where
// the allocation is also performed for you!
void ta_texture_desc_free(texture_description_t *desc);

// Given a box bounded by 4 verticies, draw it to the screen with the particular color.
// The type shold be one of TA_CMD_POLYGON_TYPE_OPAQUE, TA_CMD_POLYGON_TYPE_TRANSPARENT
// or TA_CMD_POLYGON_TYPE_PUNCHTHRU. The verticies should be specified in order of lower
// left, upper left, upper right, lower right. However, they may have affine transformations
// applied to them using matrix math should you wish to scale/rotate/shear the box.
void ta_fill_box(uint32_t type, vertex_t *verticies, color_t color);

// Given a box bounded by identical conditions to the above function, draw a sprite
// with a particular texture. All caveats and conditions from above apply here, but
// the box is drawn textured instead of filled.
void ta_draw_sprite(uint32_t type, textured_vertex_t *verticies, texture_description_t *texture);

// Draw a triangle strip consisting of striplen TA_CMD_POLYGON_STRIPLENGTH_1,
// TA_CMD_POLYGON_STRIPLENGTH_2, TA_CMD_POLYGON_STRIPLENGTH_4 or TA_CMD_POLYGON_STRIPLENGTH_6.
// The type is the same as ta_fill_box()'s type, specifying whether the triangle is opaque,
// transparent or punchthru. The verticies should be in order of bottom left, top left, bottom
// right for the first triangle, and then alternating up and down for the strip beyond that.
void ta_draw_triangle_strip(uint32_t type, uint32_t striplen, textured_vertex_t *verticies, texture_description_t *texture);
void ta_draw_triangle_strip_uv(uint32_t type, uint32_t striplen, vertex_t *verticies, uv_t *uvcoords, texture_description_t *texture);

#ifdef __cplusplus
}
#endif

#endif
