#include "naomi/video.h"
#include "naomi/system.h"
#include "naomi/timer.h"
#include "naomi/ta.h"
#include "video-internal.h"

#define MAX_H_TILE (640/32)
#define MAX_V_TILE (480/32)

struct ta_buffers {
    /* TODO Is this enough room for command lists? */
    char cmd_list[2][512 * 1024];
    /* TODO Is this enough room for opaque polygons? */
    char tile_buffer[2][TA_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE];
    /* The background vertex. */
    int background_vertex[2][24];
    /* The individual tile descriptors for the 32x32 tiles. */
    int tile_descriptor[2][6 * MAX_H_TILE * MAX_V_TILE];
};

static struct ta_buffers *ta_working_buffers = (struct ta_buffers *)0xa5400000;

/* Send a command, with len equal to either TA_LIST_SHORT or TA_LIST_LONG
 * for either 32 or 64 byte TA commands. */
void ta_commit_list(void *src, int len)
{
    hw_memcpy((void *)0xB0000000, src, len);
}

/* Set up buffers and descriptors for a tilespace */
void _ta_create_tile_descriptors(void *tile_descriptor_base, void *tile_buffer_base, int tile_width, int tile_height)
{
    /* Each tile uses 64 bytes of buffer space.  So buf must point to 64*w*h bytes of data */
    unsigned int *vr = tile_descriptor_base;
    unsigned int opaquebase = ((unsigned int)tile_buffer_base) & 0x00ffffff;

    /* Set up individual tiles. */
    for (int x = 0; x < tile_width; x++)
    {
        for (int y = 0; y < tile_height; y++)
        {
            int sob = (x == 0 && y == 0) ? 0x10000000 : 0x00000000;
            int eob = (x == (tile_width - 1) && y == (tile_height - 1)) ? 0x80000000 : 0x00000000;

            // Set start of buffer/end of buffer, set autosorted translucent polygons, set tile position
            *vr++ = sob | eob | 0x20000000 | (y << 8) | (x << 2);

            // Opaque polygons.
            *vr++ = opaquebase + ((x + (y * tile_width)) * TA_OBJECT_BUFFER_SIZE);

            // We don't support opaque modifiers, so nothing here.
            *vr++ = 0x80000000;

            // TODO: Translucent polygons.
            *vr++ = 0x80000000;

            // We don't suppport translucent modifiers, so nothing here.
            *vr++ = 0x80000000;

            // We don't support punch-through polygons, so nothing here.
            *vr++ = 0x80000000;
        }
    }
}

/* Tell the command list compiler where to store the command list,
   and which tilespace to use */
unsigned int _ta_set_target(void *cmd_list_base, void *tile_buffer_base, int tile_width, int tile_height)
{
    volatile unsigned int *regs = (volatile unsigned int *)0xa05f8000;
    unsigned int cmdl = ((unsigned int)cmd_list_base) & 0x00ffffff;
    unsigned int tbuf = ((unsigned int)tile_buffer_base) & 0x00ffffff;

    regs[0x008/4] = 1;		/* Reset TA */
    regs[0x008/4] = 0;
    regs[0x124/4] = tbuf;
    regs[0x12c/4] = 0;
    regs[0x128/4] = cmdl;
    regs[0x130/4] = 0;
    regs[0x13c/4] = ((tile_height - 1) << 16) | (tile_width - 1);
    regs[0x164/4] = tbuf;
    regs[0x140/4] = 0x00100002;
    regs[0x144/4] = 0x80000000;	/* Confirm settings */

    return regs[0x144/4];
}

void _ta_set_background(void *background)
{
    /* TODO: We need to be able to specify a background plane. */
    uint32_t *bgpointer = (uint32_t *)background;

    /* First 3 words of this are a mode1/mode2/texture word, followed by
     * 3 7-word x/y/z/u/v/base color/offset color chunks specifying the
     * bottom left, top left and bottom right of the background quad. */
    for (int i = 0; i < 24; i++)
    {
        bgpointer[i] = 0;
    }
}

// Video parameters from video.c
extern unsigned int global_video_depth;
extern unsigned int global_video_width;
extern unsigned int global_video_height;

// Which framebuffer we're on.
extern unsigned int buffer_loc;
#define next_buffer_loc ((buffer_loc) ? 0 : 1)

// Actual framebuffer address.
extern void *buffer_base;

void _ta_init_buffers()
{
    for (int i = 0; i < 2; i++)
    {
        _ta_create_tile_descriptors(ta_working_buffers->tile_descriptor[i], ta_working_buffers->tile_buffer[i], global_video_width / 32, global_video_height / 32);
        _ta_set_background(ta_working_buffers->background_vertex[i]);
    }
}

void ta_commit_begin()
{
    // Set the target of our TA commands based on the current framebuffer position.
    _ta_set_target(ta_working_buffers->cmd_list[next_buffer_loc], ta_working_buffers->tile_buffer[next_buffer_loc], global_video_width / 32, global_video_height / 32);

    // TODO: Need to inform thread system that we will be waiting for the TA to finish loading. */
}

/* Send the special end of list command to signify done sending display
 * commands to TA. Also wait for the TA to be finished processing our data. */
void ta_commit_end()
{
    unsigned int words[8] = { 0 };
    ta_commit_list(words, TA_LIST_SHORT);

    /* TODO: This should wait for the render pipeline to be filled but
     * that's an interrupt. Instead, just sleep for a bit. */
    timer_wait(2500);
}

/* Launch a new render pass */
void _ta_begin_render(void *cmd_list_base, void *tiles, void *background, void *scrn, float zclip)
{
    volatile unsigned int *regs = (volatile unsigned int *)0xa05f8000;

    unsigned int cmdl = ((unsigned int)cmd_list_base) & 0x00ffffff;
    unsigned int tls = ((unsigned int)tiles) & 0x00ffffff;
    unsigned int scn = ((unsigned int)scrn) & 0x00ffffff;
    uint32_t zclipint = ((uint32_t)zclip) & 0xFFFFFFF0;

    regs[0x02c/4] = tls;
    regs[0x020/4] = cmdl;
    regs[0x060/4] = scn;
    regs[0x064/4] = ((uint32_t)scn) + global_video_width * global_video_depth;
    regs[0x08c/4] = 0x01000000 | ((((uint32_t)background) & 0xFFFFFFFC) << 1);
    regs[0x088/4] = zclipint;
    regs[0x014/4] = 0xffffffff; /* Launch! */
}

void ta_render_begin()
{
    /* TODO: Set request to wait for TA render end here. */

    /* Start rendering the new command list to the screen */
    _ta_begin_render(
        ta_working_buffers->cmd_list[next_buffer_loc],
        ta_working_buffers->tile_descriptor[next_buffer_loc],
        ta_working_buffers->background_vertex[next_buffer_loc],
        buffer_base,
        0.2
    );
}

void ta_render_wait()
{
    /* TODO: This should wait for the render pipeline to be clear but
     * that's an interrupt. Instead, just sleep for a bit. */
    timer_wait(10000);
}

void ta_render()
{
    ta_render_begin();
    ta_render_wait();
}

/* TODO: This entire below section needs to be cleaned up */
static unsigned int three_d_params[] = {
	0x8098, 0x00800408,	/* Polygon sorting and cache sizes */
	0x8078, 0x3f800000,	/* Polygon culling (1.0f) */
	0x8084, 0x00000000,	/* Perpendicular triangle compare (0.0f) */
	0x8030, 0x00000101,	/* Span sorting enable */
	0x80b0, 0x007f7f7f,	/* Fog table color (ARGB, A is ignored) */
	0x80b4, 0x007f7f7f,	/* Fog vertex color (ARGB, A is ignored) */
	0x80c0, 0x00000000,	/* Color clamp min (ARGB) */
	0x80bc, 0xffffffff,	/* Color clamp max (ARGB) */
	0x8080, 0x00000007,	/* Pixel sampling position, everything set at (0.5, 0.5) */
	0x8074, 0x00000000,	/* Shadow scaling */
	0x807c, 0x0027df77,	/* FPU params? */
	0x8008, 0x00000001,	/* TA reset */
	0x8008, 0x00000000,	/* TA out of reset */
	0x80e4, 0x00000000,	/* stride width (TSP_CFG) */
	0x80b8, 0x0000ff07,	/* fog density */
	0x80b4, 0x007f7f7f,	/* fog vertex color */
	0x80b0, 0x007f7f7f,	/* fog table color */
	0x8108, 0x00000003  /* 32bit palette (0x0 = ARGB1555, 0x1 = RGB565, 0x2 = ARGB4444, 0x3 = ARGB8888  */
};

static int twiddletab[1024];

void _ta_init_twiddletab()
{
    for(int x=0; x<1024; x++)
    {
        twiddletab[x] = (
            (x & 1) |
            ((x & 2) << 1) |
            ((x & 4) << 2) |
            ((x & 8) << 3) |
            ((x & 16) << 4) |
            ((x & 32) << 5) |
            ((x & 64) << 6) |
            ((x & 128) << 7) |
            ((x & 256) << 8) |
            ((x & 512) << 9)
        );
    }
}

void _ta_init()
{
    volatile unsigned char *regs = (volatile unsigned char *)(void *)0xa05f0000;
    int cnt = (sizeof(three_d_params) / sizeof(three_d_params[0])) / 2;
    unsigned int *values = three_d_params;

    while(cnt--)
    {
        unsigned int r = *values++;
        unsigned int v = *values++;
        *(volatile unsigned int *)(regs + r) = v;
    }

    // Wait for vblank.
    volatile unsigned int *vbl = (volatile unsigned int *)(void *)0xa05f810c;

    while (!(*vbl & 0x01ff));
    while (*vbl & 0x01ff);

    // Initialize twiddle table for texture load operations.
    _ta_init_twiddletab();
}

void _ta_free()
{
    // Nothing for now.
}

void *ta_palette_bank(int size, int banknum)
{
    if (size == TA_PALETTE_CLUT4)
    {
        if (banknum < 0 || banknum > 63) { return 0; }

        uint32_t *palette = (uint32_t *)0xa05f9000;
        return &palette[16 * banknum];
    }
    if (size == TA_PALETTE_CLUT8)
    {
        if (banknum < 0 || banknum > 3) { return 0; }

        uint32_t *palette = (uint32_t *)0xa05f9000;
        return &palette[256 * banknum];
    }

    return 0;
}

int ta_texture_load(void *offset, int size, void *data)
{
    if (size != 8 && size != 16 && size != 32 && size != 64 && size != 128 && size != 256 && size != 512 && size != 1024)
    {
        return -1;
    }
    if (offset == 0 || data == 0)
    {
        return -1;
    }

    uint16_t *tex = (uint16_t *)(((uint32_t)offset) | UNCACHED_MIRROR);
    uint16_t *src = (uint16_t *)data;

    for(int i = 0; i < 256; i++)
    {
        for(int j = 0; j < 256; j += 2)
        {
            tex[twiddletab[i] | (twiddletab[j] >> 1)] = src[(j + (i * 256)) >> 1];
        }
    }

    return 0;
}
