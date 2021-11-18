#include <naomi/video.h>
#include <naomi/system.h>
#include <naomi/timer.h>
#include <naomi/maple.h>
#include <naomi/interrupt.h>
#include <naomi/matrix.h>
#include <naomi/ta.h>
#include <math.h>

// PVR/TA example based heavily off of the Hardware 3D example by marcus.

// Definitions for matrixes that convert from worldview to screenview.
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

void init_palette()
{
    uint32_t *palette[4] = {
        ta_palette_bank(TA_PALETTE_CLUT8, 0),
        ta_palette_bank(TA_PALETTE_CLUT8, 1),
        ta_palette_bank(TA_PALETTE_CLUT8, 2),
        ta_palette_bank(TA_PALETTE_CLUT8, 3),
    };

    for(int n = 0; n < 256; n++)
    {
        // Blue
        palette[0][n] = rgb(0, 0, n);

        // Green
        palette[1][n] = rgb(0, n, 0);

        // Purple
        palette[2][n] = rgb(n, 0, n);

        // Yellow
        palette[3][n] = rgb(n, n, 0);
    }
}

/* Draw a textured polygon for one of the faces of the cube */
void draw_face(float *p1, float *p2, float *p3, float *p4, void *tex, int pal)
{
    struct polygon_list mypoly;
    struct packed_color_vertex_list myvertex;

    mypoly.cmd =
        TA_CMD_POLYGON |
        TA_CMD_POLYGON_TYPE_OPAQUE |
        TA_CMD_POLYGON_SUBLIST |
        TA_CMD_POLYGON_STRIPLENGTH_2 |
        TA_CMD_POLYGON_PACKED_COLOR |
        TA_CMD_POLYGON_TEXTURED;
    mypoly.mode1 =
        TA_POLYMODE1_Z_ALWAYS |
        TA_POLYMODE1_CULL_CCW;
    mypoly.mode2 =
        TA_POLYMODE2_TEXTURE_DECAL |
        TA_POLYMODE2_U_SIZE_256 |
        TA_POLYMODE2_V_SIZE_256 |
        TA_POLYMODE2_TEXTURE_CLAMP_U |
        TA_POLYMODE2_TEXTURE_CLAMP_V |
        TA_POLYMODE2_FOG_DISABLED |
        TA_POLYMODE2_SRC_BLEND_ONE |
        TA_POLYMODE2_DST_BLEND_ZERO;
    mypoly.texture =
        TA_TEXTUREMODE_CLUT8 |
        TA_TEXTUREMODE_CLUTBANK8(pal) |
        TA_TEXTUREMODE_ADDRESS(tex);
    mypoly.alpha = mypoly.red = mypoly.green = mypoly.blue = 0.0;
    ta_commit_list(&mypoly, TA_LIST_SHORT);

    myvertex.cmd = TA_CMD_VERTEX;
    myvertex.color = 0xffffffff;
    myvertex.ocolor = 0;

    myvertex.x = p1[0];
    myvertex.y = p1[1];
    myvertex.z = p1[2];
    myvertex.u = 1.0;
    myvertex.v = 1.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p2[0];
    myvertex.y = p2[1];
    myvertex.z = p2[2];
    myvertex.u = 1.0;
    myvertex.v = 0.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p3[0];
    myvertex.y = p3[1];
    myvertex.z = p3[2];
    myvertex.u = 0.0;
    myvertex.v = 1.0;
    ta_commit_list(&myvertex, TA_LIST_SHORT);

    myvertex.x = p4[0];
    myvertex.y = p4[1];
    myvertex.z = p4[2];
    myvertex.u = 0.0;
    myvertex.v = 0.0;
    myvertex.cmd |= TA_CMD_VERTEX_END_OF_STRIP;
    ta_commit_list(&myvertex, TA_LIST_SHORT);
}

// 8-bit textures that we're loading per side.
extern uint8_t *tex1_png_data;
extern uint8_t *tex2_png_data;
extern uint8_t *tex3_png_data;
extern uint8_t *tex4_png_data;
extern uint8_t *tex5_png_data;
extern uint8_t *tex6_png_data;

void main()
{
    /* Set up PowerVR display and tile accelerator hardware */
    video_init_simple();
    video_set_background_color(rgb(48, 48, 48));

    /* Create palettes for our grayscale (indexed) textures */
    init_palette();

    /* TODO: We should really be able to ask the TA driver for a texture slot.
     * For now, just allocate space for the 6 256x256x8 bit textures manually. */
    uint16_t *tex[6];
    tex[0] = (unsigned short *)ta_texture_base();
    tex[1] = (void*)(((char *)tex[0])+256*256);
    tex[2] = (void*)(((char *)tex[1])+256*256);
    tex[3] = (void*)(((char *)tex[2])+256*256);
    tex[4] = (void*)(((char *)tex[3])+256*256);
    tex[5] = (void*)(((char *)tex[4])+256*256);
    ta_texture_load(tex[0], 256, tex1_png_data);
    ta_texture_load(tex[1], 256, tex2_png_data);
    ta_texture_load(tex[2], 256, tex3_png_data);
    ta_texture_load(tex[3], 256, tex4_png_data);
    ta_texture_load(tex[4], 256, tex5_png_data);
    ta_texture_load(tex[5], 256, tex6_png_data);

    /* x/y/z rotation amount in degrees */
    int i = 45;
    int j = 45;
    int k = 0;

    int count = 0;
    while (1)
    {
        /* Check buttons, rotate cube based on inputs. */
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

        /* Set up our cube. */
        float val = 1.0 + (sin((count / 30.0) * M_PI) / 32.0);
        float coords[8][3] = {
            { -val, -val, -val },
            {  val, -val, -val },
            { -val,  val, -val },
            {  val,  val, -val },
            { -val, -val,  val },
            {  val, -val,  val },
            { -val,  val,  val },
            {  val,  val,  val },
        };

        /* Set up the hardware transformation in the SH4 with the transformations we need to do */
        matrix_init_identity();

        /* TODO: These should be moved into the TA library and also take into account the screen rotation. */
        matrix_apply(&screenview_matrix);
        matrix_apply(&projection_matrix);
        matrix_translate_z(ZOFFS);

        /* Rotate the camera about the cube. */
        matrix_rotate_x(i);
        matrix_rotate_y(j);
        matrix_rotate_z(k);

        /* Apply the transformation to all the coordinates, and normalize the
           resulting homogenous coordinates into normal 3D coordinates again. */
        float trans_coords[8][3];
        matrix_transform_coords(coords, trans_coords, 8);

        /* Begin sending commands to the TA to draw stuff */
        ta_commit_begin();

        /* Draw the 6 faces of the cube */
        draw_face(trans_coords[0], trans_coords[1], trans_coords[2], trans_coords[3], tex[0], 0);
        draw_face(trans_coords[1], trans_coords[5], trans_coords[3], trans_coords[7], tex[1], 1);
        draw_face(trans_coords[4], trans_coords[5], trans_coords[0], trans_coords[1], tex[2], 2);
        draw_face(trans_coords[5], trans_coords[4], trans_coords[7], trans_coords[6], tex[3], 3);
        draw_face(trans_coords[4], trans_coords[0], trans_coords[6], trans_coords[2], tex[4], 1);
        draw_face(trans_coords[2], trans_coords[3], trans_coords[6], trans_coords[7], tex[5], 2);

        /* Mark the end of the command list */
        ta_commit_end();

        /* Now, request to render it */
        ta_render();

        /* Now, display some debugging on top of the TA. */
        video_draw_debug_text(32, 32, rgb(255, 255, 255), "Rendering with TA...\nLiveness counter: %d", count++);
        video_display_on_vblank();
    }
}
