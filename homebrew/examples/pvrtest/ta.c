#include <naomi/video.h>
#include <naomi/system.h>
#include "ta.h"

/* TODO: This file needs to go into libnaomi as well, and register definitions need adding. */

/* Send a command, with len equal to either TA_LIST_SHORT or TA_LIST_LONG
 * for either 32 or 64 byte TA commands. */
void ta_commit_list(void *src, int len)
{
    hw_memcpy((void *)0xB0000000, src, len);
}

/* Send the special end of list command to signify done sending display
 * commands to TA. */
void ta_commit_end()
{
    unsigned int words[8] = { 0 };
    ta_commit_list(words, TA_LIST_SHORT);
}

/* Set up buffers and descriptors for a tilespace */
void ta_create_tile_descriptors(void *tile_descriptor_base, void *tile_buffer_base, int tile_width, int tile_height)
{
    /* Each tile uses 64 bytes of buffer space.  So buf must point to 64*w*h bytes of data */
    unsigned int *vr = tile_descriptor_base;
    unsigned int opaquebase = ((unsigned int)tile_buffer_base) & 0x00ffffff;
    unsigned int transbase = opaquebase + ((tile_width * tile_height) * TA_OBJECT_BUFFER_SIZE);

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

            // Translucent polygons.
            *vr++ = transbase + ((x + (y * tile_width)) * TA_OBJECT_BUFFER_SIZE);

            // We don't suppport translucent modifiers, so nothing here.
            *vr++ = 0x80000000;

            // We don't support punch-through polygons, so nothing here.
            *vr++ = 0x80000000;
        }
    }
}

/* Tell the command list compiler where to store the command list,
   and which tilespace to use */
unsigned int ta_set_target(void *cmd_list_base, void *tile_buffer_base, int tile_width, int tile_height)
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

void ta_set_background(void *background)
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

/* Launch a new render pass */
void ta_begin_render(void *cmd_list_base, void *tiles, void *background, void *scrn, float zclip)
{
    volatile unsigned int *regs = (volatile unsigned int *)0xa05f8000;

    unsigned int cmdl = ((unsigned int)cmd_list_base) & 0x00ffffff;
    unsigned int tls = ((unsigned int)tiles) & 0x00ffffff;
    unsigned int scn = ((unsigned int)scrn) & 0x00ffffff;
    uint32_t zclipint = ((uint32_t)zclip) & 0xFFFFFFF0;

    int framebuffer_width;
    if (video_is_vertical())
    {
        framebuffer_width = video_height();
    }
    else
    {
        framebuffer_width = video_width();
    }

    regs[0x02c/4] = tls;
    regs[0x020/4] = cmdl;
    regs[0x060/4] = scn;
    regs[0x064/4] = ((uint32_t)scn) + framebuffer_width * video_depth();
    regs[0x08c/4] = 0x01000000 | ((((uint32_t)background) & 0xFFFFFFFC) << 1);
    regs[0x088/4] = zclipint;
    regs[0x014/4] = 0xffffffff; /* Launch! */
}
