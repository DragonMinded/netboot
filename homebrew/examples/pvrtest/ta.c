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
void *ta_create_tile_descriptors(void *tile_descriptor_base, void *tile_buffer_base, int tile_width, int tile_height)
{
    /* each tile desriptor is 6 words. In addition, there's a 24 word header */
    /* so, there are 24+6*w*h words stored at tile_descriptor_base. */

    /* each tile uses 64 bytes of buffer space.  So buf must point to */
    /* 64*w*h bytes of data */
    unsigned int *vr = tile_descriptor_base;
    unsigned int bf = ((unsigned int)tile_buffer_base)&0x00ffffff;
    unsigned int strbase = (((unsigned int)tile_descriptor_base)&0x00ffffff)|0x80000000;

    /* Set up header */
    for (int x = 0; x < 18; x++)
    {
        *vr++ = 0;
    }

    /* Set up some more unknown stuff. */
    *vr++ = 0x10000000;
    *vr++ = 0x80000000;
    *vr++ = 0x80000000;
    *vr++ = 0x80000000;
    *vr++ = 0x80000000;
    *vr++ = 0x80000000;

    /* Set up individual tiles. */
    for (int x = 0; x < tile_width; x++)
    {
        for (int y = 0; y < tile_height; y++)
        {
            *vr++ = (y << 8) | (x << 2);
            *vr++ = bf + ((x + (y * tile_width)) << 6);
            *vr++ = strbase;
            *vr++ = strbase;
            *vr++ = strbase;
            *vr++ = strbase;
        }
    }

    /* Mark the end of tile descriptor bit */
    vr[-6] |= 0x80000000;

    /* Pass this to ta_begin_render() tiles parameter */
    return ((char *)tile_descriptor_base) + (18 * 4);
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

/* Launch a new render pass */
void ta_begin_render(void *cmd_list_base, void *tiles, void *scrn, float zclip)
{
    volatile unsigned int *regs = (volatile unsigned int *)0xa05f8000;

    unsigned int cmdl = ((unsigned int)cmd_list_base) & 0x00ffffff;
    unsigned int tls = ((unsigned int)tiles) & 0x00ffffff;
    unsigned int scn = ((unsigned int)scrn) & 0x00ffffff;
    uint32_t zclipint = ((uint32_t)zclip) & 0xFFFFFFF0;

    unsigned int *taend = (unsigned int *)(void *)(0xa5000000 | regs[0x138/4]);

    for (int i = 0; i < 18; i++)
    {
        taend[i] = 0;
    }

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
    regs[0x08c/4] = 0x01000000 | ((((char *)taend) - ((char *)cmd_list_base)) << 1);
    regs[0x088/4] = zclipint;
    regs[0x014/4] = 0xffffffff; /* Launch! */
}
