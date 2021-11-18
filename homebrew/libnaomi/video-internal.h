#ifndef __VIDEO_INTERNAL_H
#define __VIDEO_INTERNAL_H

// Internal video defines shared between all video modules. Do not import or use this file.
#define SET_PIXEL_V_2(base, x, y, color) ((uint16_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)] = (color) & 0xFFFF
#define SET_PIXEL_H_2(base, x, y, color) ((uint16_t *)(base))[(x) + ((y) * global_video_width)] = (color) & 0xFFFF
#define SET_PIXEL_V_4(base, x, y, color) ((uint32_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)] = (color)
#define SET_PIXEL_H_4(base, x, y, color) ((uint32_t *)(base))[(x) + ((y) * global_video_width)] = (color)

#define GET_PIXEL_V_2(base, x, y) ((uint16_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)]
#define GET_PIXEL_H_2(base, x, y) ((uint16_t *)(base))[(x) + ((y) * global_video_width)]
#define GET_PIXEL_V_4(base, x, y) ((uint32_t *)(base))[(global_video_width - (y)) + ((x) * global_video_width)]
#define GET_PIXEL_H_4(base, x, y) ((uint32_t *)(base))[(x) + ((y) * global_video_width)]

#define RGB0555(r, g, b) ((((b) >> 3) & (0x1F << 0)) | (((g) << 2) & (0x1F << 5)) | (((r) << 7) & (0x1F << 10)) | 0x8000)
#define RGB1555(r, g, b, a) ((((b) >> 3) & (0x1F << 0)) | (((g) << 2) & (0x1F << 5)) | (((r) << 7) & (0x1F << 10)) | (((a) << 8) & 0x8000))
#define RGB0888(r, g, b) ((b) & 0xFF) | (((g) << 8) & 0xFF00) | (((r) << 16) & 0xFF0000) | 0xFF000000
#define RGB8888(r, g, b, a) ((b) & 0xFF) | (((g) << 8) & 0xFF00) | (((r) << 16) & 0xFF0000) | (((a) << 24) & 0xFF000000)

// Convert back to 8-bit values, setting the lower 3 bits to the high
// bits so that values closer to 255 will be brighter and values closer
// to 0 will be darker.
#define EXPLODE0555(color, r, g, b) do { \
    unsigned int bint = (color) & 0x1F; \
    unsigned int gint = ((color) >> 5) & 0x1F; \
    unsigned int rint = ((color) >> 10) & 0x1F; \
    r = (rint << 3) | (rint >> 2); \
    g = (gint << 3) | (gint >> 2); \
    b = (bint << 3) | (bint >> 2); \
} while (0)
#define EXPLODE1555(color, r, g, b, a) do { \
    unsigned int bint = (color) & 0x1F; \
    unsigned int gint = ((color) >> 5) & 0x1F; \
    unsigned int rint = ((color) >> 10) & 0x1F; \
    r = (rint << 3) | (rint >> 2); \
    g = (gint << 3) | (gint >> 2); \
    b = (bint << 3) | (bint >> 2); \
    a = ((color) & 0x8000) ? 255 : 0; \
} while (0)
#define EXPLODE0888(color, r, g, b) do { \
    b = (color) & 0xFF; \
    g = ((color) >> 8) & 0xFF; \
    r = ((color) >> 16) & 0xFF; \
} while (0)
#define EXPLODE8888(color, r, g, b, a) do { \
    b = (color) & 0xFF; \
    g = ((color) >> 8) & 0xFF; \
    r = ((color) >> 16) & 0xFF; \
    a = ((color) >> 24) & 0xFF; \
} while (0)

// Shared between TA and video implementation.
void _ta_init();
void _ta_free();
void _ta_init_buffers();

// Register definitions shared between TA and video implementation.
#define POWERVR2_BASE 0xA05F8000
#define POWERVR2_PALETTE_BASE 0xA05F9000

#define POWERVR2_ID (0x000 >> 2)
#define POWERVR2_REVISION (0x004 >> 2)
#define POWERVR2_RESET (0x008 >> 2)
#define POWERVR2_START_RENDER (0x014 >> 2)
#define POWERVR2_CMDLIST_ADDR (0x020 >> 2)
#define POWERVR2_TILES_ADDR (0x02C >> 2)
#define POWERVR2_TA_SPANSORT (0x030 >> 2)
#define POWERVR2_BORDER_COL (0x040 >> 2)
#define POWERVR2_FB_DISPLAY_CFG (0x044 >> 2)
#define POWERVR2_FB_RENDER_CFG (0x048 >> 2)
#define POWERVR2_FB_RENDER_MODULO (0x04C >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_1 (0x050 >> 2)
#define POWERVR2_FB_DISPLAY_ADDR_2 (0x054 >> 2)
#define POWERVR2_FB_DISPLAY_SIZE (0x05C >> 2)
#define POWERVR2_TA_FRAMEBUFFER_ADDR_1 (0x060 >> 2)
#define POWERVR2_TA_FRAMEBUFFER_ADDR_2 (0x064 >> 2)
#define POWERVR2_FB_CLIP_X (0x068 >> 2)
#define POWERVR2_FB_CLIP_Y (0x06C >> 2)
#define POWERVR2_SHADOW_SCALING (0x074 >> 2)
#define POWERVR2_TA_POLYGON_CULL (0x078 >> 2)
#define POWERVR2_TA_FPU_PARAMS (0x07C >> 2)
#define POWERVR2_PIXEL_SAMPLE (0x080 >> 2)
#define POWERVR2_TA_PERPENDICULAR_TRI (0x084 >> 2)
#define POWERVR2_BACKGROUND_CLIP (0x088 >> 2)
#define POWERVR2_BACKGROUND_INSTRUCTIONS (0x08C >> 2)
#define POWERVR2_TA_CACHE_SIZES (0x098 >> 2)
#define POWERVR2_VRAM_CFG1 (0x0A0 >> 2)
#define POWERVR2_VRAM_CFG2 (0x0A4 >> 2)
#define POWERVR2_VRAM_CFG3 (0x0A8 >> 2)
#define POWERVR2_FOG_TABLE_COLOR (0x0B0 >> 2)
#define POWERVR2_FOG_VERTEX_COLOR (0x0B4 >> 2)
#define POWERVR2_FOG_DENSITY (0x0B4 >> 2)
#define POWERVR2_COLOR_CLAMP_MAX (0x0BC >> 2)
#define POWERVR2_COLOR_CLAMP_MIN (0x0C0 >> 2)
#define POWERVR2_VBLANK_INTERRUPT (0x0CC >> 2)
#define POWERVR2_SYNC_CFG (0x0D0 >> 2)
#define POWERVR2_HBLANK (0x0D4 >> 4)
#define POWERVR2_SYNC_LOAD (0x0D8 >> 2)
#define POWERVR2_VBORDER (0x0DC >> 2)
#define POWERVR2_TSP_CFG (0x0E4 >> 2)
#define POWERVR2_VIDEO_CFG (0x0E8 >> 2)
#define POWERVR2_HPOS (0x0EC >> 2)
#define POWERVR2_VPOS (0x0F0 >> 2)
#define POWERVR2_PALETTE_MODE (0x108 >> 2)
#define POWERVR2_SYNC_STAT (0x10C >> 2)
#define POWERVR2_OBJBUF_BASE (0x124 >> 2)
#define POWERVR2_CMDLIST_BASE (0x128 >> 2)
#define POWERVR2_OBJBUF_LIMIT (0x12C >> 2)
#define POWERVR2_CMDLIST_LIMIT (0x130 >> 2)
#define POWERVR2_TILE_CLIP (0x13C >> 2)
#define POWERVR2_TA_BLOCKSIZE (0x140 >> 2)
#define POWERVR2_TA_CONFIRM (0x144 >> 2)
#define POWERVR2_ADDITIONAL_OBJBUF (0x164 >> 2)

#define DISPLAY_CFG_RGB1555 0
#define DISPLAY_CFG_RGB565 1
#define DISPLAY_CFG_RGB888 2
#define DISPLAY_CFG_RGB0888 3

#define RENDER_CFG_RGB0555 0
#define RENDER_CFG_RGB565 1
#define RENDER_CFG_ARGB4444 2
#define RENDER_CFG_ARGB1555 3
#define RENDER_CFG_RGB888 4
#define RENDER_CFG_RGB0888 5
#define RENDER_CFG_ARGB8888 6
// Mode 7 appears to be a redefinition of mode 2.

#define PALETTE_CFG_ARGB1555 0
#define PALETTE_CFG_RGB565 1
#define PALETTE_CFG_ARGB4444 2
#define PALETTE_CFG_ARGB8888 3

#define BLOCKSIZE_NOT_USED 0
#define BLOCKSIZE_32 1
#define BLOCKSIZE_64 2
#define BLOCKSIZE_128 3

#endif
