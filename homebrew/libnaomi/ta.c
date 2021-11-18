#include <string.h>
#include "naomi/video.h"
#include "naomi/system.h"
#include "naomi/timer.h"
#include "naomi/thread.h"
#include "naomi/interrupt.h"
#include "naomi/ta.h"
#include "irqinternal.h"
#include "holly.h"
#include "video-internal.h"

#define WAITING_LIST_OPAQUE 0x1
#define WAITING_LIST_TRANSPARENT 0x2
#define WAITING_LIST_PUNCHTHRU 0x4

static unsigned int waiting_lists = 0;

/* Send a command, with len equal to either TA_LIST_SHORT or TA_LIST_LONG
 * for either 32 or 64 byte TA commands. */
void ta_commit_list(void *src, int len)
{
    /* Figure out what kind of command this is so we can set up to wait for
     * it to be finished loading properly. */
    if (!_irq_is_disabled(_irq_get_sr()))
    {
        uint32_t command = ((uint32_t *)src)[0];

        if ((command & TA_CMD_POLYGON) != 0)
        {
            if ((command & TA_CMD_POLYGON_TYPE_OPAQUE) != 0)
            {
                if ((waiting_lists & WAITING_LIST_OPAQUE) == 0)
                {
                    waiting_lists |= WAITING_LIST_OPAQUE;
                    thread_notify_wait_ta_load_opaque();
                }
            }
            if ((command & TA_CMD_POLYGON_TYPE_TRANSPARENT) != 0)
            {
                if ((waiting_lists & WAITING_LIST_TRANSPARENT) == 0)
                {
                    waiting_lists |= WAITING_LIST_TRANSPARENT;
                    thread_notify_wait_ta_load_transparent();
                }
            }
            if ((command & TA_CMD_POLYGON_TYPE_PUNCHTHRU) != 0)
            {
                if ((waiting_lists & WAITING_LIST_PUNCHTHRU) == 0)
                {
                    waiting_lists |= WAITING_LIST_PUNCHTHRU;
                    thread_notify_wait_ta_load_punchthru();
                }
            }
        }
    }

    hw_memcpy((void *)0xB0000000, src, len);
}

struct ta_buffers {
    /* Command lists. */
    void *cmd_list;
    int cmd_list_size;
    /* Opaque polygons */
    void *opaque_object_buffer;
    int opaque_object_buffer_size;
    /* Transparent polygons */
    void *transparent_object_buffer;
    int transparent_object_buffer_size;
    /* Punch-Thru polygons */
    void *punchthru_object_buffer;
    int punchthru_object_buffer_size;
    /* The background vertex. */
    void *background_vertex;
    /* The individual tile descriptors for the 32x32 tiles. */
    void *tile_descriptors;
    /* The safe spot to start storing texxtures in RAM. */
    void *texture_ram;
};

/* Set up buffers and descriptors for a tilespace */
void _ta_create_tile_descriptors(struct ta_buffers *buffers, int tile_width, int tile_height)
{
    /* Each tile uses 64 bytes of buffer space.  So buf must point to 64*w*h bytes of data */
    unsigned int *vr = buffers->tile_descriptors;
    unsigned int opaquebase = ((unsigned int)buffers->opaque_object_buffer) & 0x00ffffff;
    unsigned int transparentbase = ((unsigned int)buffers->transparent_object_buffer) & 0x00ffffff;
    unsigned int punchthrubase = ((unsigned int)buffers->punchthru_object_buffer) & 0x00ffffff;

    /* It seems the hardware needs a dummy tile or it renders the first tile weird. */
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
            // Set end of buffer, set autosorted translucent polygons, set tile position
            int eob = (x == (tile_width - 1) && y == (tile_height - 1)) ? 0x80000000 : 0x00000000;
            *vr++ = eob | 0x20000000 | (y << 8) | (x << 2);

            // Opaque polygons.
            if (buffers->opaque_object_buffer_size)
            {
                *vr++ = opaquebase + ((x + (y * tile_width)) * buffers->opaque_object_buffer_size);
            }
            else
            {
                *vr++ = 0x80000000;
            }

            // We don't support opaque modifiers, so nothing here.
            *vr++ = 0x80000000;

            // Translucent polygons.
            if (buffers->transparent_object_buffer_size)
            {
                *vr++ = transparentbase + ((x + (y * tile_width)) * buffers->transparent_object_buffer_size);
            }
            else
            {
                *vr++ = 0x80000000;
            }

            // We don't suppport translucent modifiers, so nothing here.
            *vr++ = 0x80000000;

            // Punch-through (or solid/transparent-only) polygons.
            if (buffers->punchthru_object_buffer_size)
            {
                *vr++ = punchthrubase + ((x + (y * tile_width)) * buffers->punchthru_object_buffer_size);
            }
            else
            {
                *vr++ = 0x80000000;
            }
        }
    }
}

/* Tell the command list compiler where to store the command list, and which tilespace to use */
void _ta_set_target(struct ta_buffers *buffers, int tile_width, int tile_height)
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;
    unsigned int cmdl = ((unsigned int)buffers->cmd_list) & 0x00ffffff;
    unsigned int objbuf = ((unsigned int)buffers->opaque_object_buffer) & 0x00ffffff;

    /* Reset TA */
    videobase[POWERVR2_RESET] = 1;
    videobase[POWERVR2_RESET] = 0;

    /* Set the tile buffer base in the TA */
    videobase[POWERVR2_OBJBUF_BASE] = objbuf;
    videobase[POWERVR2_OBJBUF_LIMIT] = 0;

    /* Set the command list base in the TA */
    videobase[POWERVR2_CMDLIST_BASE] = cmdl;
    videobase[POWERVR2_CMDLIST_LIMIT] = cmdl + buffers->cmd_list_size;

    /* Set the number of tiles we have in the tile descriptor. */
    videobase[POWERVR2_TILE_CLIP] = ((tile_height - 1) << 16) | (tile_width - 1);

    /* Set the location for object buffers if we run out in our tile descriptors. */
    videobase[POWERVR2_ADDITIONAL_OBJBUF] = objbuf;

    /* Figure out blocksizes for below. */
    int opaque_blocksize = BLOCKSIZE_NOT_USED;
    if (buffers->opaque_object_buffer_size == 32)
    {
        opaque_blocksize = BLOCKSIZE_32;
    }
    else if (buffers->opaque_object_buffer_size == 64)
    {
        opaque_blocksize = BLOCKSIZE_64;
    }
    else if (buffers->opaque_object_buffer_size == 128)
    {
        opaque_blocksize = BLOCKSIZE_128;
    }

    int transparent_blocksize = BLOCKSIZE_NOT_USED;
    if (buffers->transparent_object_buffer_size == 32)
    {
        transparent_blocksize = BLOCKSIZE_32;
    }
    else if (buffers->transparent_object_buffer_size == 64)
    {
        transparent_blocksize = BLOCKSIZE_64;
    }
    else if (buffers->transparent_object_buffer_size == 128)
    {
        transparent_blocksize = BLOCKSIZE_128;
    }

    int punchthru_blocksize = BLOCKSIZE_NOT_USED;
    if (buffers->punchthru_object_buffer_size == 32)
    {
        punchthru_blocksize = BLOCKSIZE_32;
    }
    else if (buffers->punchthru_object_buffer_size == 64)
    {
        punchthru_blocksize = BLOCKSIZE_64;
    }
    else if (buffers->punchthru_object_buffer_size == 128)
    {
        punchthru_blocksize = BLOCKSIZE_128;
    }

    /* Set up object block sizes and such. */
    videobase[POWERVR2_TA_BLOCKSIZE] = (
        (1 << 20) |                     // Grow downward in memory
        (punchthru_blocksize << 16) |   // Punch-through polygon blocksize
        (BLOCKSIZE_NOT_USED << 12) |    // Translucent polygon modifier blocksize
        (transparent_blocksize << 8) |  // Translucent polygon blocksize
        (BLOCKSIZE_NOT_USED << 4) |     // Opaque polygon modifier blocksize
        (opaque_blocksize << 0)         // Opaque polygon blocksize
    );

    /* Confirm the above settings. */
    videobase[POWERVR2_TA_CONFIRM] = 0x80000000;
}

void _ta_clear_background(struct ta_buffers *buffers)
{
    /* TODO: We need to be able to specify a background plane with a solid color or image. */
    uint32_t *bgpointer = (uint32_t *)buffers->background_vertex;

    /* First 3 words of this are a mode1/mode2/texture word, followed by
     * 3 7-word x/y/z/u/v/base color/offset color chunks specifying the
     * bottom left, top left and bottom right of the background quad. */
    for (int i = 0; i < 24; i ++)
    {
        bgpointer[i] = 0;
    }
}

// Video parameters from video.c
extern unsigned int global_video_depth;
extern unsigned int global_video_width;
extern unsigned int global_video_height;

// Actual framebuffer address.
extern void *buffer_base;

#define MAX_H_TILE (640/32)
#define MAX_V_TILE (480/32)
#define TA_OPAQUE_OBJECT_BUFFER_SIZE 64
#define TA_TRANSPARENT_OBJECT_BUFFER_SIZE 64
#define TA_PUNCHTHRU_OBJECT_BUFFER_SIZE 64
#define TA_CMDLIST_SIZE (512 * 1024)
#define TA_BACKGROUND_SIZE (24 * 4)

static struct ta_buffers ta_working_buffers;
// Alignment required for various buffers.
#define BUFFER_ALIGNMENT 128
#define ENSURE_ALIGNMENT(x) (((x) + (BUFFER_ALIGNMENT - 1)) & (~(BUFFER_ALIGNMENT - 1)))
#define BUFLOC 0xA5400000

void _ta_init_buffers()
{
    // Where we start with our buffers.
    uint32_t curbufloc = BUFLOC;

    // Clear our structure out.
    memset(&ta_working_buffers, 0, sizeof(ta_working_buffers));

    // First, allocate space for the polygon object buffers.
    ta_working_buffers.opaque_object_buffer = (void *)curbufloc;
    ta_working_buffers.opaque_object_buffer_size = TA_OPAQUE_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_OPAQUE_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));

    // TODO: For some reason enabling these causes the renderer to go in super slow motion
#if 0
    ta_working_buffers.transparent_object_buffer = (void *)curbufloc;
    ta_working_buffers.transparent_object_buffer_size = TA_TRANSPARENT_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_TRANSPARENT_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));

    ta_working_buffers.punchthru_object_buffer = (void *)curbufloc;
    ta_working_buffers.punchthru_object_buffer_size = TA_PUNCHTHRU_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_PUNCHTHRU_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));
#endif

    // Now, allocate space for the command buffer.
    ta_working_buffers.cmd_list = (void *)curbufloc;
    ta_working_buffers.cmd_list_size = TA_CMDLIST_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + TA_CMDLIST_SIZE);

    // Now, grab some space for the background buffer instruction.
    ta_working_buffers.background_vertex = (void *)curbufloc;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + TA_BACKGROUND_SIZE);

    // Finally, grab space for the tile descriptors.
    ta_working_buffers.tile_descriptors = (void *)curbufloc;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (4 * (6 * ((MAX_H_TILE * MAX_V_TILE) + 1))));

    // Now, the remaining space can be used for texture RAM.
    ta_working_buffers.texture_ram = (void *)((curbufloc & 0x00FFFFFF) | 0xA4000000);

    // Finally, clear the memory so we don't get artifacts.
    if (hw_memset((void *)BUFLOC, 0, curbufloc - BUFLOC) == 0)
    {
        memset((void *)BUFLOC, 0, curbufloc - BUFLOC);
    }

    _ta_create_tile_descriptors(&ta_working_buffers, global_video_width / 32, global_video_height / 32);
    _ta_clear_background(&ta_working_buffers);
}

void ta_commit_begin()
{
    // Set the target of our TA commands based on the current framebuffer position.
    _ta_set_target(&ta_working_buffers, global_video_width / 32, global_video_height / 32);
    waiting_lists = 0;
}

/* Send the special end of list command to signify done sending display
 * commands to TA. Also wait for the TA to be finished processing our data. */
void ta_commit_end()
{
    /* Avoid going through the TA command lookup */
    unsigned int words[8] = { 0 };
    hw_memcpy((void *)0xB0000000, words, TA_LIST_SHORT);

    if (_irq_is_disabled(_irq_get_sr()))
    {
        /* Just spinloop waiting for the interrupt to happen. */
        if (waiting_lists && WAITING_LIST_OPAQUE)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED;
        }

        if (waiting_lists && WAITING_LIST_TRANSPARENT)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED;
        }
        if (waiting_lists && WAITING_LIST_PUNCHTHRU)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED;
        }
    }
    else
    {
        if (waiting_lists && WAITING_LIST_OPAQUE)
        {
            thread_wait_ta_load_opaque();
        }
        if (waiting_lists && WAITING_LIST_TRANSPARENT)
        {
            thread_wait_ta_load_transparent();
        }
        if (waiting_lists && WAITING_LIST_PUNCHTHRU)
        {
            thread_wait_ta_load_punchthru();
        }
    }
}

/* Launch a new render pass */
void _ta_begin_render(void *cmd_list_base, void *tiles, void *background, void *scrn, float zclip)
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    unsigned int cmdl = ((unsigned int)cmd_list_base) & 0x00ffffff;
    unsigned int tls = ((unsigned int)tiles) & 0x00ffffff;
    unsigned int scn = ((unsigned int)scrn) & 0x00ffffff;
    unsigned int bg = ((unsigned int)background - (unsigned int)cmd_list_base) & 0x00fffffc;
    uint32_t zclipint = ((uint32_t)zclip) & 0xFFFFFFF0;

    /* Set up current render tiledescriptions, commandlist and framebuffer to render to. */
    videobase[POWERVR2_TILES_ADDR] = tls;
    videobase[POWERVR2_CMDLIST_ADDR] = cmdl;
    videobase[POWERVR2_TA_FRAMEBUFFER_ADDR_1] = scn;
    videobase[POWERVR2_TA_FRAMEBUFFER_ADDR_2] = ((uint32_t)scn) + global_video_width * global_video_depth;

    /* Set up background plane for where there aren't triangles/quads to draw. */
    videobase[POWERVR2_BACKGROUND_INSTRUCTIONS] = (
        (1 << 24) |  // Disable texture for background plane.
        (bg << 1)    // Background plane instructions pointer.
    );
    videobase[POWERVR2_BACKGROUND_CLIP] = zclipint;

    /* Launch the render sequence. */
    videobase[POWERVR2_START_RENDER] = 0xffffffff;
}

void ta_render_begin()
{
    if (!_irq_is_disabled(_irq_get_sr()))
    {
        /* Notify thread/interrupt system that we will want to wait for the TA to finish rendering. */
        thread_notify_wait_ta_render_finished();
    }

    /* Start rendering the new command list to the screen */
    _ta_begin_render(
        ta_working_buffers.cmd_list,
        ta_working_buffers.tile_descriptors,
        ta_working_buffers.background_vertex,
        buffer_base,
        0.001
    );
}

void ta_render_wait()
{
    if (_irq_is_disabled(_irq_get_sr()))
    {
        /* Just spinloop waiting for the interrupt to happen. */
        while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED)) { ; }
        HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED;
    }
    else
    {
        /* Now, park the thread until the renderer is finished. */
        thread_wait_ta_render_finished();
    }
}

void ta_render()
{
    ta_render_begin();
    ta_render_wait();
}

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
    uint32_t old_interrupts = irq_disable();
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Set up sorting, culling and comparison configuration.
    videobase[POWERVR2_TA_CACHE_SIZES] = (
        (0x200 << 14) |  // Translucent cache size.
        (0x200 << 4) |   // Punch-through cache size.
        (1 << 3) |       // Enable polygon discard.
        (0 << 0)         // Auto-sort translucent triangles.
    );

    // Culling set at 1.0f
    videobase[POWERVR2_TA_POLYGON_CULL] = 0x3f800000;

    // Perpendicular triangle compare set at 0.0f
    videobase[POWERVR2_TA_PERPENDICULAR_TRI] = 0x0;

    // Enable span and offset sorting
    videobase[POWERVR2_TA_SPANSORT] = (
        (1 << 8) |  // Offset sort enabled.
        (1 << 0)    // Span sort enabled.
    );

    // Set up fog registers
    videobase[POWERVR2_FOG_TABLE_COLOR] = RGB0888(127, 127, 127);
    videobase[POWERVR2_FOG_VERTEX_COLOR] = RGB0888(127, 127, 127);

    // Set up color clamping registers
    videobase[POWERVR2_COLOR_CLAMP_MIN] = RGB8888(0, 0, 0, 0);
    videobase[POWERVR2_COLOR_CLAMP_MAX] = RGB8888(255, 255, 255, 255);

    // Place pixel sampling position at (0.5, 0.5) instead of (0.0, 0.0)
    videobase[POWERVR2_PIXEL_SAMPLE] = 0x7;

    // Disable shadow scaling
    videobase[POWERVR2_SHADOW_SCALING] = 0x0;

    // Set up unknown FPU parameters
    videobase[POWERVR2_TA_FPU_PARAMS] = 0x0027df77;

    // Reset the TA
    videobase[POWERVR2_RESET] = 1;
    videobase[POWERVR2_RESET] = 0;

    // Set stride width to zero for stride-based textures
    videobase[POWERVR2_TSP_CFG] = 0x0;

    // Set up fog registers (again?)
    videobase[POWERVR2_FOG_DENSITY] = 0xFF07;
    videobase[POWERVR2_FOG_VERTEX_COLOR] = RGB0888(127, 127, 127);
    videobase[POWERVR2_FOG_TABLE_COLOR] = RGB0888(127, 127, 127);

    // Set up palettes to match videomode so that we can use rgb()/rgba() to fill palettes
    videobase[POWERVR2_PALETTE_MODE] = global_video_depth == 2 ? PALETTE_CFG_ARGB1555 : PALETTE_CFG_ARGB8888;

    // Wait for vblank.
    while(!(videobase[POWERVR2_SYNC_STAT] & 0x1FF)) { ; }
    while((videobase[POWERVR2_SYNC_STAT] & 0x1FF)) { ; }

    // Enable TA finished loading and rendering interrupts.
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED;
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED;
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED;
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED) == 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK | HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED;
    }

    // Initialize twiddle table for texture load operations.
    _ta_init_twiddletab();

    irq_restore(old_interrupts);
}

void _ta_free()
{
    uint32_t old_interrupts = irq_disable();
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_TSP_RENDER_FINISHED);
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED);
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED);
    }
    if ((HOLLY_INTERNAL_IRQ_2_MASK & HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED) != 0)
    {
        HOLLY_INTERNAL_IRQ_2_MASK = HOLLY_INTERNAL_IRQ_2_MASK & (~HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED);
    }
    irq_restore(old_interrupts);
}

void *ta_palette_bank(int size, int banknum)
{
    if (size == TA_PALETTE_CLUT4)
    {
        if (banknum < 0 || banknum > 63) { return 0; }

        uint32_t *palette = (uint32_t *)POWERVR2_PALETTE_BASE;
        return &palette[16 * banknum];
    }
    if (size == TA_PALETTE_CLUT8)
    {
        if (banknum < 0 || banknum > 3) { return 0; }

        uint32_t *palette = (uint32_t *)POWERVR2_PALETTE_BASE;
        return &palette[256 * banknum];
    }

    return 0;
}

void *ta_texture_base()
{
    return ta_working_buffers.texture_ram;
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
