
/* Pixel formats for ta_begin_render() (should be same as the screens) */

#define TA_PIXFMT_RGB555   0
#define TA_PIXFMT_RGB565   1
#define TA_PIXFMT_ARGB4444 2
#define TA_PIXFMT_ARGB1555 3
#define TA_PIXFMT_RGB888   5
#define TA_PIXFMT_ARGB8888 6

#define TA_PIXFMT_DITHER   8



/* TA commands... */


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

#define TA_CMD_USERCLIP 0x20000000


/* Command: Polygon / Modifier volume */

struct polygon_list
{
  unsigned int cmd;
  unsigned int mode1;
  unsigned int mode2;
  unsigned int texture;
  /* used with intensity type color */
  float alpha;
  float red;
  float green;
  float blue;
};
		    
struct modifier_list
{
    unsigned int cmd;
    unsigned int instruction;
    int not_used[6];
};

/* Command: Vertex */

struct packed_color_vertex_list
{
    unsigned int cmd;
    float x;
    float y;
    float z;
    float u;
    float v;
    unsigned int color;
    unsigned int ocolor;
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


#define TA_OBJECT_BUFFER_SIZE 64

extern unsigned int ta_set_target(void *cmdlist, void *tilebuf, int tile_width, int tile_height);
extern void ta_set_background(void *background);
extern void ta_create_tile_descriptors(void *ptr, void *buf, int tile_width, int tile_height);
extern void ta_wait_render();
extern void ta_begin_render(void *cmdlist, void *tiles, void *background, void *scrn, float zclip);


#define TA_LIST_SHORT 32
#define TA_LIST_LONG 64


extern void ta_commit_list(void *list, int size);
extern void ta_commit_end();
