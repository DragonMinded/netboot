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

/* What lists we populated and need to wait to finish filling. */
static unsigned int waiting_lists = 0;

/* What lists we populated ever, during this frame. */
static unsigned int populated_lists = 0;

/* The background color as set by a user when requesting a different background. */
static uint32_t ta_background_color = 0;

/* Send a command, with len equal to either TA_LIST_SHORT or TA_LIST_LONG
 * for either 32 or 64 byte TA commands. */
void ta_commit_list(void *src, int len)
{
    /* Figure out what kind of command this is so we can set up to wait for
     * it to be finished loading properly. */
    if (!_irq_is_disabled(_irq_get_sr()))
    {
        uint32_t command = ((uint32_t *)src)[0];

        if ((command & 0xE0000000) == TA_CMD_POLYGON)
        {
            if ((command & 0x07000000) == TA_CMD_POLYGON_TYPE_OPAQUE)
            {
                if ((waiting_lists & (WAITING_LIST_TRANSPARENT | WAITING_LIST_PUNCHTHRU)) != 0)
                {
                    _irq_display_invariant("display list failure", "cannot send more than one type of polygon in single list!");
                }
                if ((waiting_lists & WAITING_LIST_OPAQUE) == 0)
                {
                    waiting_lists |= WAITING_LIST_OPAQUE;
                    populated_lists |= WAITING_LIST_OPAQUE;
                    thread_notify_wait_ta_load_opaque();
                }
            }
            else if ((command & 0x07000000) == TA_CMD_POLYGON_TYPE_TRANSPARENT)
            {
                if ((waiting_lists & (WAITING_LIST_OPAQUE | WAITING_LIST_PUNCHTHRU)) != 0)
                {
                    _irq_display_invariant("display list failure", "cannot send more than one type of polygon in single list!");
                }
                if ((waiting_lists & WAITING_LIST_TRANSPARENT) == 0)
                {
                    waiting_lists |= WAITING_LIST_TRANSPARENT;
                    populated_lists |= WAITING_LIST_TRANSPARENT;
                    thread_notify_wait_ta_load_transparent();
                }
            }
            else if ((command & 0x07000000) == TA_CMD_POLYGON_TYPE_PUNCHTHRU)
            {
                if ((waiting_lists & (WAITING_LIST_TRANSPARENT | WAITING_LIST_OPAQUE)) != 0)
                {
                    _irq_display_invariant("display list failure", "cannot send more than one type of polygon in single list!");
                }
                if ((waiting_lists & WAITING_LIST_PUNCHTHRU) == 0)
                {
                    waiting_lists |= WAITING_LIST_PUNCHTHRU;
                    populated_lists |= WAITING_LIST_PUNCHTHRU;
                    thread_notify_wait_ta_load_punchthru();
                }
            }
            else
            {
                _irq_display_invariant("display list failure", "we do not support this type of polygon!");
            }
        }
    }

    hw_memcpy((void *)0xB0000000, src, len);
}

struct ta_buffers
{
    /* Command lists. */
    void *cmd_list;
    int cmd_list_size;
    /* Background command list. Cleverly stuck where we otherwise needed a buffer */
    void *background_list;
    int background_list_size;
    /* Additional object buffers for overflow. */
    void *overflow_buffer;
    int overflow_buffer_size;
    /* Opaque polygons */
    void *opaque_object_buffer;
    int opaque_object_buffer_size;
    /* Transparent polygons */
    void *transparent_object_buffer;
    int transparent_object_buffer_size;
    /* Punch-Thru polygons */
    void *punchthru_object_buffer;
    int punchthru_object_buffer_size;
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
    uint32_t last_address = 0;
    for (int x = 0; x < tile_width; x++)
    {
        for (int y = 0; y < tile_height; y++)
        {
            // Set end of buffer, set tile position
            int eob = (x == (tile_width - 1) && y == (tile_height - 1)) ? 0x80000000 : 0x00000000;
            *vr++ = eob | (y << 8) | (x << 2);

            // Opaque polygons.
            if (buffers->opaque_object_buffer_size > 0 && (populated_lists & WAITING_LIST_OPAQUE) != 0)
            {
                last_address = opaquebase + ((x + (y * tile_width)) * buffers->opaque_object_buffer_size);
                *vr++ = last_address;
            }
            else
            {
                *vr++ = 0x80000000 | last_address;
            }

            // We don't support opaque modifiers, so nothing here.
            *vr++ = 0x80000000 | last_address;

            // Translucent polygons.
            if (buffers->transparent_object_buffer_size > 0 && (populated_lists & WAITING_LIST_TRANSPARENT) != 0)
            {
                last_address = transparentbase + ((x + (y * tile_width)) * buffers->transparent_object_buffer_size);
                *vr++ = last_address;
            }
            else
            {
                *vr++ = 0x80000000 | last_address;
            }

            // We don't suppport translucent modifiers, so nothing here.
            *vr++ = 0x80000000 | last_address;

            // Punch-through (or solid/transparent-only) polygons.
            if (buffers->punchthru_object_buffer_size > 0 && (populated_lists & WAITING_LIST_PUNCHTHRU) != 0)
            {
                last_address = punchthrubase + ((x + (y * tile_width)) * buffers->punchthru_object_buffer_size);
                *vr++ = last_address;
            }
            else
            {
                *vr++ = 0x80000000 | last_address;
            }
        }
    }
}

/* Tell the command list compiler where to store the command list, and which tilespace to use */
uint32_t _ta_set_target(struct ta_buffers *buffers, int tile_width, int tile_height)
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;
    unsigned int cmdl = ((unsigned int)buffers->cmd_list) & 0x00ffffff;
    unsigned int objbuf = ((unsigned int)buffers->overflow_buffer) & 0x00ffffff;

    /* Reset TA */
    videobase[POWERVR2_RESET] = 0x3;
    videobase[POWERVR2_RESET] = 0x0;

    /* Set the tile buffer base in the TA, grows downward. */
    videobase[POWERVR2_OBJBUF_BASE] = objbuf + buffers->overflow_buffer_size;
    videobase[POWERVR2_OBJBUF_LIMIT] = objbuf;

    /* Set the command list base in the TA, grows upward. */
    videobase[POWERVR2_CMDLIST_BASE] = cmdl;
    videobase[POWERVR2_CMDLIST_LIMIT] = cmdl + buffers->cmd_list_size;

    /* Set the number of tiles we have in the tile descriptor. */
    videobase[POWERVR2_TILE_CLIP] = ((tile_height - 1) << 16) | (tile_width - 1);

    /* Set the location for object buffers if we run out in our tile descriptors. */
    videobase[POWERVR2_ADDITIONAL_OBJBUF] = objbuf + buffers->overflow_buffer_size;

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

    /* Perform a dummy read that won't get optimized away. */
    return videobase[POWERVR2_TA_CONFIRM];
}

#define BACKGROUND_Z_PLANE 0.000001

// Video parameters from video.c
extern unsigned int global_video_depth;
extern unsigned int global_video_width;
extern unsigned int global_video_height;

void _ta_set_background_color(struct ta_buffers *buffers, uint32_t rgba)
{
    if (buffers->background_list == 0)
    {
        // We aren't initialized!
        return;
    }

    uint32_t *bgintpointer = (uint32_t *)buffers->background_list;
    float *bgfltpointer = (float *)bgintpointer;

    /* First 3 words of this are a mode1/mode2/texture word, followed by
     * 3 7-word x/y/z/u/v/base color/offset color chunks specifying the
     * first three vertexes of the quad. */
    int loc = 0;
    bgintpointer[loc++] =
        TA_POLYMODE1_Z_GREATER |
        TA_POLYMODE1_GOURAD_SHADED;
    bgintpointer[loc++] =
        TA_POLYMODE2_SRC_BLEND_ONE |
        TA_POLYMODE2_DST_BLEND_ZERO |
        TA_POLYMODE2_FOG_DISABLED |
        TA_POLYMODE2_DISABLE_TEX_ALPHA |
        TA_POLYMODE2_MIPMAP_D_1_00 |
        TA_POLYMODE2_TEXTURE_MODULATE;
    bgintpointer[loc++] = 0;

    bgfltpointer[loc++] = 0.0;
    bgfltpointer[loc++] = 0.0;
    bgfltpointer[loc++] = BACKGROUND_Z_PLANE;
    bgintpointer[loc++] = rgba;

    bgfltpointer[loc++] = (float)global_video_width;
    bgfltpointer[loc++] = 0.0;
    bgfltpointer[loc++] = BACKGROUND_Z_PLANE;
    bgintpointer[loc++] = rgba;

    bgfltpointer[loc++] = 0.0;
    bgfltpointer[loc++] = (float)global_video_height;
    bgfltpointer[loc++] = BACKGROUND_Z_PLANE;
    bgintpointer[loc++] = rgba;
}

static struct ta_buffers ta_working_buffers;

void ta_set_background_color(color_t color)
{
    // Set the color to the background plane.
    _ta_set_background_color(&ta_working_buffers, RGB0888(color.r, color.g, color.b));
}

// Actual framebuffer address.
extern void *buffer_base;
extern uint32_t global_buffer_offset[3];

#define MAX_H_TILE (640/32)
#define MAX_V_TILE (480/32)
#define TA_OPAQUE_OBJECT_BUFFER_SIZE 128
#define TA_TRANSPARENT_OBJECT_BUFFER_SIZE 128
#define TA_PUNCHTHRU_OBJECT_BUFFER_SIZE 64
#define TA_CMDLIST_SIZE (1 * 1024 * 1024)
#define TA_BACKGROUNDLIST_SIZE 256
#define TA_OVERFLOW_SIZE (1 * 1024 * 1024)

// Alignment required for various buffers.
#define BUFFER_ALIGNMENT 128
#define ENSURE_ALIGNMENT(x) (((x) + (BUFFER_ALIGNMENT - 1)) & (~(BUFFER_ALIGNMENT - 1)))

// Prototype from texture.c for managing textures in VRAM.
void _ta_init_texture_allocator();

void _ta_init_buffers()
{
    // Where we start with our buffers. Its important that BUFLOC is aligned
    // to a 1MB boundary (masking with 0xFFFFF should give all 0's). It should
    // be safe to calculate where to put this based on the framebuffer locations,
    // but for some reason this results in stomped on texture RAM.
    uint32_t bufloc = (((global_buffer_offset[2] & VRAM_MASK) | UNCACHED_MIRROR | VRAM_BASE) + 0xFFFFF) & 0xFFF00000;
    uint32_t curbufloc = bufloc;

    // Clear our structure out.
    memset(&ta_working_buffers, 0, sizeof(ta_working_buffers));

    // First, allocate space for the command buffer. Give it some padding so that the
    // extra object buffer limit is not the same as our command buffer limit.
    ta_working_buffers.cmd_list = (void *)curbufloc;
    ta_working_buffers.cmd_list_size = TA_CMDLIST_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + TA_CMDLIST_SIZE);

    // Now, allocate space between the two, both for padding and for the background plane.
    ta_working_buffers.background_list = (void *)curbufloc;
    ta_working_buffers.background_list_size = TA_BACKGROUNDLIST_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + TA_BACKGROUNDLIST_SIZE);

    // Now, allocate space for extra object buffer overflow.
    ta_working_buffers.overflow_buffer = (void *)curbufloc;
    ta_working_buffers.overflow_buffer_size = TA_OVERFLOW_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + TA_OVERFLOW_SIZE);

    // Now, allocate space for the polygon object buffers.
    ta_working_buffers.opaque_object_buffer = (void *)curbufloc;
    ta_working_buffers.opaque_object_buffer_size = TA_OPAQUE_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_OPAQUE_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));

    ta_working_buffers.transparent_object_buffer = (void *)curbufloc;
    ta_working_buffers.transparent_object_buffer_size = TA_TRANSPARENT_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_TRANSPARENT_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));

    ta_working_buffers.punchthru_object_buffer = (void *)curbufloc;
    ta_working_buffers.punchthru_object_buffer_size = TA_PUNCHTHRU_OBJECT_BUFFER_SIZE;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (TA_PUNCHTHRU_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE));

    // Finally, grab space for the tile descriptors themselves.
    ta_working_buffers.tile_descriptors = (void *)curbufloc;
    curbufloc = ENSURE_ALIGNMENT(curbufloc + (4 * (6 * ((MAX_H_TILE * MAX_V_TILE) + 1))));

    // Now, the remaining space can be used for texture RAM.
    ta_working_buffers.texture_ram = (void *)((curbufloc & VRAM_MASK) | UNCACHED_MIRROR | TEXRAM_BASE);

    // Clear the above memory so we don't get artifacts.
    if (hw_memset((void *)bufloc, 0, curbufloc - bufloc) == 0)
    {
        memset((void *)bufloc, 0, curbufloc - bufloc);
    }

    // Finally, add a command to the command buffer that we will point at for the background polygon.
    _ta_set_background_color(&ta_working_buffers, ta_background_color);

    // Now, ask the texture allocator to initialize based on our known texture location.
    _ta_init_texture_allocator();
}

void ta_commit_begin()
{
    if (populated_lists == 0)
    {
        // Set the target of our TA commands based on the current framebuffer position.
        // Don't do this if we've already sent it for this frame.
        _ta_set_target(&ta_working_buffers, global_video_width / 32, global_video_height / 32);
    }

    // We are not waiting on anything, we will find out what we're about to wait on
    // as soon as we get a list through ta_commit_list().
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
        if (waiting_lists & WAITING_LIST_OPAQUE)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_OPAQUE_FINISHED;
        }

        if (waiting_lists & WAITING_LIST_TRANSPARENT)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_TRANSPARENT_FINISHED;
        }
        if (waiting_lists & WAITING_LIST_PUNCHTHRU)
        {
            while (!(HOLLY_INTERNAL_IRQ_STATUS & HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED)) { ; }
            HOLLY_INTERNAL_IRQ_STATUS = HOLLY_INTERNAL_INTERRUPT_TRANSFER_PUNCHTHRU_FINISHED;
        }
    }
    else
    {
        if (waiting_lists & WAITING_LIST_OPAQUE)
        {
            thread_wait_ta_load_opaque();
        }
        if (waiting_lists & WAITING_LIST_TRANSPARENT)
        {
            thread_wait_ta_load_transparent();
        }
        if (waiting_lists & WAITING_LIST_PUNCHTHRU)
        {
            thread_wait_ta_load_punchthru();
        }
    }

    /* Reset this here, just incase. */
    waiting_lists = 0;
}

union intfloat
{
    float f;
    uint32_t i;
};

extern void _video_set_ta_registers();

/* Launch a new render pass */
void _ta_begin_render(struct ta_buffers *buffers, void *scrn, float zclip)
{
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    unsigned int cmdl = ((unsigned int)buffers->cmd_list) & VRAM_MASK;
    unsigned int tls = ((unsigned int)buffers->tile_descriptors) & VRAM_MASK;
    unsigned int scn = ((unsigned int)scrn) & VRAM_MASK;
    unsigned int bgl = (unsigned int)buffers->background_list - cmdl;

    /* Actually populate the tile descriptors themselves, pointing at the object buffers we just allocated.
     * We do this here every frame so we can exclude list types for lists that we definitely have no
     * polygons for. */
    _ta_create_tile_descriptors(&ta_working_buffers, global_video_width / 32, global_video_height / 32);

    /* Convert the bits from float to int so we can cap off the bottom 4 bits. */
    union intfloat f2i;
    f2i.f = zclip;
    uint32_t zclipint = (f2i.i) & 0xFFFFFFF0;

    /* Set up current render tiledescriptions, commandlist and framebuffer to render to. */
    videobase[POWERVR2_TILES_ADDR] = tls;
    videobase[POWERVR2_CMDLIST_ADDR] = cmdl;
    videobase[POWERVR2_TA_FRAMEBUFFER_ADDR_1] = scn;
    videobase[POWERVR2_TA_FRAMEBUFFER_ADDR_2] = scn + global_video_width * global_video_depth;

    /* Set up background plane for where there aren't triangles/quads to draw. */
    videobase[POWERVR2_BACKGROUND_INSTRUCTIONS] = (
        (1 << 24) |                // Span for the background plane vertexes? Appears to be (this number + 3) words per vertex.
        ((bgl & 0xfffffc) << 1)    // Background plane instructions pointer, we stick it at the beginning of the command buffer.
    );
    videobase[POWERVR2_BACKGROUND_CLIP] = zclipint;

    /* Reset the TA registers that appear to change per-frame. */
    _video_set_ta_registers();

    /* Launch the render sequence. */
    videobase[POWERVR2_START_RENDER] = 0xffffffff;

    /* Now that we rendered, clear our populated list tracker. */
    populated_lists = 0;
}

void ta_render_begin()
{
    if (!_irq_is_disabled(_irq_get_sr()))
    {
        /* Notify thread/interrupt system that we will want to wait for the TA to finish rendering. */
        thread_notify_wait_ta_render_finished();
    }

    /* Start rendering the new command list to the screen */
    _ta_begin_render(&ta_working_buffers, buffer_base, BACKGROUND_Z_PLANE);
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

// Prototype for initializing texture twiddle tables in texture.c
void _ta_init_twiddletab();

void _ta_init()
{
    uint32_t old_interrupts = irq_disable();
    volatile unsigned int *videobase = (volatile unsigned int *)POWERVR2_BASE;

    // Make sure we clear out our working code.
    memset(&ta_working_buffers, 0, sizeof(ta_working_buffers));
    ta_background_color = RGB0888(0, 0, 0);

    // Set up sorting, culling and comparison configuration.
    videobase[POWERVR2_TA_CACHE_SIZES] = (
        (0x200 << 14) |  // Translucent cache size.
        (0x40 << 4) |    // Punch-through cache size.
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

    // Reset the TA and PVR.
    videobase[POWERVR2_RESET] = 0x3;
    videobase[POWERVR2_RESET] = 0x0;

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

    // Initialize our list waiting tracking varaibles.
    waiting_lists = 0;
    populated_lists = 0;

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

uint32_t ta_palette_entry(color_t color)
{
    if (global_video_depth == 2)
    {
        return RGB1555(color.r, color.g, color.b, color.a);
    }
    else
    {
        return RGB8888(color.r, color.g, color.b, color.a);
    }
}

void *ta_texture_base()
{
    return ta_working_buffers.texture_ram;
}

uint32_t _ta_texture_desc_uvsize(int uvsize)
{
    switch(uvsize)
    {
        case 8:
            return TA_POLYMODE2_U_SIZE_8 | TA_POLYMODE2_V_SIZE_8;
        case 16:
            return TA_POLYMODE2_U_SIZE_16 | TA_POLYMODE2_V_SIZE_16;
        case 32:
            return TA_POLYMODE2_U_SIZE_32 | TA_POLYMODE2_V_SIZE_32;
        case 64:
            return TA_POLYMODE2_U_SIZE_64 | TA_POLYMODE2_V_SIZE_64;
        case 128:
            return TA_POLYMODE2_U_SIZE_128 | TA_POLYMODE2_V_SIZE_128;
        case 256:
            return TA_POLYMODE2_U_SIZE_256 | TA_POLYMODE2_V_SIZE_256;
        case 512:
            return TA_POLYMODE2_U_SIZE_512 | TA_POLYMODE2_V_SIZE_512;
        case 1024:
            return TA_POLYMODE2_U_SIZE_1024 | TA_POLYMODE2_V_SIZE_1024;
        default:
            return 0xFFFFFFFF;
    }
}

texture_description_t *ta_texture_desc_paletted(void *offset, int uvsize, int size, int banknum)
{
    texture_description_t *desc = malloc(sizeof(texture_description_t));
    if (desc)
    {
        desc->vram_location = offset;
        desc->uvsize = _ta_texture_desc_uvsize(uvsize);
        if (desc->uvsize == 0xFFFFFFFF)
        {
            free(desc);
            return 0;
        }
        desc->vram_owned = 0;

        switch(size)
        {
            case TA_PALETTE_CLUT4:
                desc->texture_mode = TA_TEXTUREMODE_CLUT4 | TA_TEXTUREMODE_CLUTBANK4(banknum);
                break;
            case TA_PALETTE_CLUT8:
                desc->texture_mode = TA_TEXTUREMODE_CLUT8 | TA_TEXTUREMODE_CLUTBANK8(banknum);
                break;
            default:
                free(desc);
                return 0;
        }
    }

    return desc;
}

texture_description_t *ta_texture_desc_direct(void *offset, int uvsize, uint32_t mode)
{
    texture_description_t *desc = malloc(sizeof(texture_description_t));
    if (desc)
    {
        desc->vram_location = offset;
        desc->uvsize = _ta_texture_desc_uvsize(uvsize);
        if (desc->uvsize == 0xFFFFFFFF)
        {
            free(desc);
            return 0;
        }
        desc->vram_owned = 0;

        switch(mode)
        {
            case TA_TEXTUREMODE_ARGB1555:
                desc->texture_mode = TA_TEXTUREMODE_ARGB1555;
                break;
            case TA_TEXTUREMODE_RGB565:
                desc->texture_mode = TA_TEXTUREMODE_RGB565;
                break;
            case TA_TEXTUREMODE_ARGB4444:
                desc->texture_mode = TA_TEXTUREMODE_ARGB4444;
                break;
            default:
                free(desc);
                return 0;
        }
    }

    return desc;
}

texture_description_t *ta_texture_desc_malloc_paletted(int uvsize, void *data, int size, int banknum)
{
    texture_description_t *desc = malloc(sizeof(texture_description_t));
    if (desc)
    {
        desc->uvsize = _ta_texture_desc_uvsize(uvsize);
        if (desc->uvsize == 0xFFFFFFFF)
        {
            free(desc);
            return 0;
        }
        desc->vram_owned = 1;

        switch(size)
        {
            case TA_PALETTE_CLUT4:
                desc->texture_mode = TA_TEXTUREMODE_CLUT4 | TA_TEXTUREMODE_CLUTBANK4(banknum);
                desc->vram_location = ta_texture_malloc(uvsize, 4);
                ta_texture_load(desc->vram_location, uvsize, 4, data);
                break;
            case TA_PALETTE_CLUT8:
                desc->texture_mode = TA_TEXTUREMODE_CLUT8 | TA_TEXTUREMODE_CLUTBANK8(banknum);
                desc->vram_location = ta_texture_malloc(uvsize, 8);
                ta_texture_load(desc->vram_location, uvsize, 8, data);
                break;
            default:
                free(desc);
                return 0;
        }
    }

    return desc;
}

texture_description_t *ta_texture_desc_malloc_direct(int uvsize, void *data, uint32_t mode)
{
    texture_description_t *desc = malloc(sizeof(texture_description_t));
    if (desc)
    {
        desc->uvsize = _ta_texture_desc_uvsize(uvsize);
        if (desc->uvsize == 0xFFFFFFFF)
        {
            free(desc);
            return 0;
        }
        desc->vram_owned = 1;

        switch(mode)
        {
            case TA_TEXTUREMODE_ARGB1555:
                desc->texture_mode = TA_TEXTUREMODE_ARGB1555;
                desc->vram_location = ta_texture_malloc(uvsize, 16);
                ta_texture_load(desc->vram_location, uvsize, 16, data);
                break;
            case TA_TEXTUREMODE_RGB565:
                desc->texture_mode = TA_TEXTUREMODE_RGB565;
                desc->vram_location = ta_texture_malloc(uvsize, 16);
                ta_texture_load(desc->vram_location, uvsize, 16, data);
                break;
            case TA_TEXTUREMODE_ARGB4444:
                desc->texture_mode = TA_TEXTUREMODE_ARGB4444;
                desc->vram_location = ta_texture_malloc(uvsize, 16);
                ta_texture_load(desc->vram_location, uvsize, 16, data);
                break;
            default:
                free(desc);
                return 0;
        }
    }

    return desc;
}

void ta_texture_desc_free(texture_description_t *desc)
{
    if (desc->vram_owned)
    {
        ta_texture_free(desc->vram_location);
    }

    // Attempt to avoid use-afrer-free issues.
    memset(desc, 0, sizeof(desc));
    free(desc);
}

void ta_fill_box(uint32_t type, vertex_t *verticies, color_t color)
{
    struct polygon_list_sprite mypoly;
    struct vertex_list_sprite myvertex;

    mypoly.cmd =
        TA_CMD_SPRITE |
        type |
        TA_CMD_POLYGON_SUBLIST |
        TA_CMD_POLYGON_STRIPLENGTH_2 |
        TA_CMD_POLYGON_PACKED_COLOR |
        TA_CMD_POLYGON_16BIT_UV;
    mypoly.mode1 =
        TA_POLYMODE1_Z_ALWAYS |
        TA_POLYMODE1_CULL_DISABLED;
    mypoly.mode2 =
        TA_POLYMODE2_MIPMAP_D_1_00 |
        TA_POLYMODE2_FOG_DISABLED |
        TA_POLYMODE2_SRC_BLEND_SRC_ALPHA |
        TA_POLYMODE2_DST_BLEND_INV_SRC_ALPHA;
    mypoly.texture = 0;
    mypoly.mult_color = RGB8888(color.r, color.g, color.b, color.a);
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
    ta_commit_list(&myvertex, TA_LIST_LONG);
}

void ta_draw_triangle_strip(uint32_t type, uint32_t striplen, textured_vertex_t *verticies, texture_description_t *texture)
{
    struct polygon_list_packed_color mypoly;
    struct vertex_list_packed_color_32bit_uv myvertex;

    mypoly.cmd =
        TA_CMD_POLYGON |
        type |
        TA_CMD_POLYGON_SUBLIST |
        striplen |
        TA_CMD_POLYGON_PACKED_COLOR |
        TA_CMD_POLYGON_TEXTURED;
    mypoly.mode1 =
        TA_POLYMODE1_Z_ALWAYS |
        TA_POLYMODE1_CULL_CW;
    mypoly.mode2 =
        TA_POLYMODE2_MIPMAP_D_1_00 |
        TA_POLYMODE2_TEXTURE_DECAL |
        texture->uvsize |
        TA_POLYMODE2_TEXTURE_CLAMP_U |
        TA_POLYMODE2_TEXTURE_CLAMP_V |
        TA_POLYMODE2_FOG_DISABLED |
        TA_POLYMODE2_SRC_BLEND_SRC_ALPHA |
        TA_POLYMODE2_DST_BLEND_INV_SRC_ALPHA;
    mypoly.texture =
        texture->texture_mode |
        TA_TEXTUREMODE_ADDRESS(texture->vram_location);
    ta_commit_list(&mypoly, TA_LIST_SHORT);

    myvertex.cmd = TA_CMD_VERTEX;
    myvertex.mult_color = 0xffffffff;
    myvertex.add_color = 0;

    myvertex.x = verticies[0].x;
    myvertex.y = verticies[0].y;
    myvertex.z = verticies[0].z;
    myvertex.u = verticies[0].u;
    myvertex.v = verticies[0].v;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = verticies[1].x;
    myvertex.y = verticies[1].y;
    myvertex.z = verticies[1].z;
    myvertex.u = verticies[1].u;
    myvertex.v = verticies[1].v;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = verticies[2].x;
    myvertex.y = verticies[2].y;
    myvertex.z = verticies[2].z;
    myvertex.u = verticies[2].u;
    myvertex.v = verticies[2].v;
    if (striplen == TA_CMD_POLYGON_STRIPLENGTH_1)
    {
        myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
        ta_commit_list(&myvertex, TA_LIST_SHORT);
    }
    else
    {
        ta_commit_list(&myvertex, TA_LIST_SHORT);

        myvertex.x = verticies[3].x;
        myvertex.y = verticies[3].y;
        myvertex.z = verticies[3].z;
        myvertex.u = verticies[3].u;
        myvertex.v = verticies[3].v;
        if (striplen == TA_CMD_POLYGON_STRIPLENGTH_2)
        {
            myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
            ta_commit_list(&myvertex, TA_LIST_SHORT);
        }
        else
        {
            ta_commit_list(&myvertex, TA_LIST_SHORT);

            myvertex.x = verticies[4].x;
            myvertex.y = verticies[4].y;
            myvertex.z = verticies[4].z;
            myvertex.u = verticies[4].u;
            myvertex.v = verticies[4].v;
            ta_commit_list(&myvertex, TA_LIST_SHORT);

            myvertex.x = verticies[5].x;
            myvertex.y = verticies[5].y;
            myvertex.z = verticies[5].z;
            myvertex.u = verticies[5].u;
            myvertex.v = verticies[5].v;
            if (striplen == TA_CMD_POLYGON_STRIPLENGTH_4)
            {
                myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
                ta_commit_list(&myvertex, TA_LIST_SHORT);
            }
            else
            {
                ta_commit_list(&myvertex, TA_LIST_SHORT);

                myvertex.x = verticies[6].x;
                myvertex.y = verticies[6].y;
                myvertex.z = verticies[6].z;
                myvertex.u = verticies[6].u;
                myvertex.v = verticies[6].v;
                ta_commit_list(&myvertex, TA_LIST_SHORT);

                myvertex.x = verticies[7].x;
                myvertex.y = verticies[7].y;
                myvertex.z = verticies[7].z;
                myvertex.u = verticies[7].u;
                myvertex.v = verticies[7].v;
                myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
                ta_commit_list(&myvertex, TA_LIST_SHORT);
            }
        }
    }
}

void ta_draw_sprite(uint32_t type, textured_vertex_t *verticies, texture_description_t *texture)
{

}

void ta_draw_sprite_uv(uint32_t type, vertex_t *verticies, uv_t *texcoords, texture_description_t *texture)
{

}
