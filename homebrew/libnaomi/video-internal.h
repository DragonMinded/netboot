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

#endif
