#include <naomi/video.h>
#include <naomi/system.h>
#include <naomi/timer.h>
#include <naomi/maple.h>
#include <naomi/interrupt.h>
#include <math.h>
#include "video.h"
#include "ta.h"
#include "matrix.h"


/*
 *    Hardware 3D example by marcus
 *
 *  This example creates a texture mapped cube
 *  using the tile accelerator hardware and the
 *  built in matrix multiplication feature of
 *  the SH4.  It was inspired by Dan's 3dtest
 *  program of course, but this one is more
 *  "clean", and does real 3D.  :-)
 *
 *  <URL:http://mc.pp.se/dc/>
 */


/** 3D operations **/


/* coordinates for the cube */

float coords[8][3] = {
    { -1.0, -1.0, -1.0 },
    {  1.0, -1.0, -1.0 },
    { -1.0,  1.0, -1.0 },
    {  1.0,  1.0, -1.0 },
    { -1.0, -1.0,  1.0 },
    {  1.0, -1.0,  1.0 },
    { -1.0,  1.0,  1.0 },
    {  1.0,  1.0,  1.0 },
};


/* transformed coordinates */

float trans_coords[8][3];


/* matrices for transforming world coordinates to
   screen coordinates (with perspective)        */

#define XCENTER 320.0
#define YCENTER 240.0

#define COT_FOVY_2 1.73 /* cot(FOVy / 2) */
#define ZNEAR 1.0
#define ZFAR  100.0

#define ZOFFS 5.0


float screenview_matrix[4][4] = {
    { YCENTER,     0.0,   0.0,   0.0 },
    {     0.0, YCENTER,   0.0,   0.0 },
    {     0.0,     0.0,   1.0 ,  0.0 },
    { XCENTER, YCENTER,   0.0,   1.0 },
};

float projection_matrix[4][4] = {
    { COT_FOVY_2,         0.0,                        0.0,   0.0 },
    {        0.0,  COT_FOVY_2,                        0.0,   0.0 },
    {        0.0,         0.0,  (ZFAR+ZNEAR)/(ZNEAR-ZFAR),  -1.0 },
    {        0.0,         0.0,  2*ZFAR*ZNEAR/(ZNEAR-ZFAR),   1.0 },
};

float translation_matrix[4][4] = {
    { 1.0,   0.0,    0.0,   0.0 },
    { 0.0,   1.0,    0.0,   0.0 },
    { 0.0,   0.0,    1.0,   0.0 },
    { 0.0,   0.0,  ZOFFS,   1.0 },  
};

/* rotation functions */

void rotate_x(float n)
{
    static float matrix[4][4] = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 },
    };
    matrix[1][1] = matrix[2][2] = cos((n / 180.0) * M_PI);
    matrix[1][2] = -(matrix[2][1] = sin((n / 180.0) * M_PI));
    apply_matrix(&matrix);
}

void rotate_y(float n)
{
    static float matrix[4][4] = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 },
    };
    matrix[0][0] = matrix[2][2] = cos((n / 180.0) * M_PI);
    matrix[2][0] = -(matrix[0][2] = sin((n / 180.0) * M_PI));
    apply_matrix(&matrix);
}

void rotate_z(float n)
{
    static float matrix[4][4] = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 },
    };
    matrix[0][0] = matrix[1][1] = cos((n / 180.0) * M_PI);
    matrix[0][1] = -(matrix[1][0] = sin((n / 180.0) * M_PI));
    apply_matrix(&matrix);
}


/** Texture operations **/


/* setup a table for easy twiddling of texures.
   (palette based textures can't be non-twiddled) */

int twiddletab[1024];

void init_twiddletab()
{
    for(int x=0; x<1024; x++)
    {
        twiddletab[x] = (
            (x&1) | 
            ((x&2)<<1) | 
            ((x&4)<<2) | 
            ((x&8)<<3) | 
            ((x&16)<<4) |
            ((x&32)<<5) |
            ((x&64)<<6) |
            ((x&128)<<7) |
            ((x&256)<<8) |
            ((x&512)<<9)
        );
    }
}

/** Palette operations **/

void init_palette()
{
    unsigned int (*palette)[4][256] = (unsigned int (*)[4][256])0xa05f9000;
    for(int n = 0; n<256; n++) {
        (*palette)[0][n] = 0xff000000 | (n & 0xFF);
        (*palette)[1][n] = 0xff000000 | ((n << 8) & 0xFF00);
        (*palette)[2][n] = 0xff000000 | (((n << 16) | n) & 0xFF00FF);
    }
}

/* Draw a textured polygon for one of the faces of the cube */

void draw_face(float *p1, float *p2, float *p3, float *p4, void *tex, int pal)
{
    struct polygon_list mypoly;
    struct packed_color_vertex_list myvertex;

    mypoly.cmd =
        TA_CMD_POLYGON_OR_MODIFIER | TA_CMD_POLYGON_TYPE_OPAQUE | TA_CMD_POLYGON_SUBLIST |
        TA_CMD_POLYGON_STRIPLENGTH_2 | TA_CMD_POLYGON_PACKED_COLOR | TA_CMD_POLYGON_TEXTURED;
    mypoly.mode1 = TA_POLYMODE1_Z_ALWAYS | TA_POLYMODE1_CULL_CCW;
    mypoly.mode2 =
        TA_POLYMODE2_BLEND_DEFAULT|TA_POLYMODE2_FOG_DISABLED|
        TA_POLYMODE2_TEXTURE_CLAMP_U|TA_POLYMODE2_TEXTURE_CLAMP_V|
        TA_POLYMODE2_BILINEAR_FILTER|TA_POLYMODE2_MIPMAP_D_1_00|
        TA_POLYMODE2_TEXTURE_REPLACE|TA_POLYMODE2_U_SIZE_256|TA_POLYMODE2_V_SIZE_256;
    mypoly.texture =
        TA_TEXTUREMODE_CLUT8|TA_TEXTUREMODE_CLUTBANK8(pal)|
        TA_TEXTUREMODE_TWIDDLED|TA_TEXTUREMODE_ADDRESS(tex);
    mypoly.alpha = mypoly.red = mypoly.green = mypoly.blue = 0.0;
    ta_commit_list(&mypoly, TA_LIST_SHORT);

    myvertex.cmd = TA_CMD_VERTEX;
    myvertex.color = 0xffffffff;
    myvertex.ocolor = 0;

    myvertex.x = p1[0];
    myvertex.y = p1[1];
    myvertex.z = p1[2];
    myvertex.u = 1.0;
    myvertex.v = 0.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p2[0];
    myvertex.y = p2[1];
    myvertex.z = p2[2];
    myvertex.u = 0.0;
    myvertex.v = 0.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p3[0];
    myvertex.y = p3[1];
    myvertex.z = p3[2];
    myvertex.u = 1.0;
    myvertex.v = 1.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p4[0];
    myvertex.y = p4[1];
    myvertex.z = p4[2];
    myvertex.u = 0.0;
    myvertex.v = 1.0;
    myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
    ta_commit_list(&myvertex, TA_LIST_SHORT);
}


/* Define space for the display command list, and the tile work area */

#define MAX_H_TILE (640/32)
#define MAX_V_TILE (480/32)

struct ta_buffers {
    /* TODO Is this enough room for command lists? */
    char cmd_list[512 * 1024];
    /* TODO Is this enough room for translucent and opaque polygons? */
    char tile_buffer[2 * TA_OBJECT_BUFFER_SIZE * MAX_H_TILE * MAX_V_TILE];
    int tile_descriptor[24 + (6 * MAX_H_TILE * MAX_V_TILE)];
};


extern uint8_t *tex1_png_data;
extern uint8_t *tex2_png_data;


void main()
{
    struct ta_buffers *bufs = (struct ta_buffers *)(void *)0xa5400000;
    unsigned short *tex[2];
    void *tiles;

    /* Set up PowerVR display and tile accelerator hardware */
    init_pvr();
    video_init_simple();

    /* Create palettes and twidding table for textures */
    init_palette();
    init_twiddletab();

    /* Just allocate space for the two 256x256x8 bit textures manually */
    tex[0] = (unsigned short *)(void *)0xa4400000;
    tex[1] = (void*)(((char *)tex[0])+256*256);

    /* Create the textures.  Unfortunatly, it's not possible to do byte
       writes to the texture memory.  So for these 8bpp textures, we have
       to write two pixels at a time.  Fortunatelty, the twiddling algorithm
       (palette based textures can not be non-twiddled) keeps horizontally
       adjacent pixel pairs together, so it's not a real problem. */
    for(int i=0; i<256; i++)
    {
        for(int j=0; j<256; j+=2)
        {
            tex[0][twiddletab[i]|(twiddletab[j]>>1)] = (tex1_png_data[j + 1 + (i * 256)] << 8) | tex1_png_data[j + (i * 256)];
            tex[1][twiddletab[i]|(twiddletab[j]>>1)] = (tex2_png_data[j + 1 + (i * 256)] << 8) | tex2_png_data[j + (i * 256)];
        }
    }

    int framebuffer_width;
    int framebuffer_height;
    if (video_is_vertical())
    {
        framebuffer_width = video_height();
        framebuffer_height = video_width();
    }
    else
    {
        framebuffer_width = video_width();
        framebuffer_height = video_height();
    }

    /* Create two sets of tile descriptors, to do double buffering */
    tiles = ta_create_tile_descriptors(bufs->tile_descriptor, bufs->tile_buffer, framebuffer_width / 32, framebuffer_height / 32);

    int i = 0;
    int j = 0;
    int k = 0;
    int count = 0;
    while (1)
    {
        maple_poll_buttons();
        jvs_buttons_t buttons = maple_buttons_held();
        if (buttons.player1.button1)
        {
            i++;
        }
        if (buttons.player1.button2)
        {
            j++;
        }
        if (buttons.player1.button3)
        {
            k++;
        }
        if (buttons.player1.button4)
        {
            i--;
        }
        if (buttons.player1.button5)
        {
            j--;
        }
        if (buttons.player1.button6)
        {
            k--;
        }

        /* Set up the hardware transformation in the
           SH4 with the transformations we need to do */
        uint32_t oldirq = irq_disable();
        clear_matrix();
        apply_matrix(&screenview_matrix);
        apply_matrix(&projection_matrix);
        apply_matrix(&translation_matrix);
        rotate_x(i / 180.0);
        rotate_y(j / 180.0);
        rotate_z(k / 180.0);

        /* Apply the transformation to all the coordinates, and normalize the
           resulting homogenous coordinates into normal 3D coordinates again. */
        transform_coords(coords, trans_coords, 8);
        irq_restore(oldirq);

        /* Clear section of screen in case the TA isn't running */
        video_fill_box(0, 0, video_width(), 64, rgb(0, 0, 0));

        /* TODO: Set request to wait for TA commit end here. */

        /* Set up the command list compiler to use the right set of buffers */
        ta_set_target(bufs->cmd_list, bufs->tile_buffer, framebuffer_width / 32, framebuffer_height / 32);

        /* Draw the 6 faces of the cube */
        draw_face(trans_coords[0], trans_coords[1], trans_coords[2], trans_coords[3], tex[0], 0);
        draw_face(trans_coords[1], trans_coords[5], trans_coords[3], trans_coords[7], tex[0], 1);
        draw_face(trans_coords[4], trans_coords[5], trans_coords[0], trans_coords[1], tex[0], 2);
        draw_face(trans_coords[5], trans_coords[4], trans_coords[7], trans_coords[6], tex[1], 0);
        draw_face(trans_coords[4], trans_coords[0], trans_coords[6], trans_coords[2], tex[1], 1);
        draw_face(trans_coords[2], trans_coords[3], trans_coords[6], trans_coords[7], tex[1], 2);

        /* Mark the end of the command list */
        ta_commit_end();

        /* TODO: This should wait for the render pipeline to be filled but
         * that's an interrupt. Instead, just sleep for a bit. */
        timer_wait(2500);

        /* TODO: Set request to wait for TA render end here. */

        /* Start rendering the new command list to the screen */
        ta_begin_render(bufs->cmd_list, tiles, video_framebuffer(), 0.2);

        /* TODO: This should wait for the render pipeline to be clear but
         * that's an interrupt. Instead, just sleep for a bit. */
        timer_wait(10000);

        video_draw_debug_text(32, 32, rgb(255, 255, 255), "Rendering with TA...\nLiveness counter: %d", count++);
        video_display_on_vblank();
    }
}
