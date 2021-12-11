#include "naomi/interrupt.h"
#include "naomi/matrix.h"
#include "naomi/video.h"
#include <math.h>

#define MAX_MATRIXES 16

static int matrixpos = 0;
static matrix_t sysmatrix[MAX_MATRIXES];

void matrix_init_identity()
{
    uint32_t old_irq = irq_disable();

    // Set up the identity matrix in XMTRX, which will look like the following:
    // 1.0, 0.0, 0.0, 0.0
    // 0.0, 1.0, 0.0, 0.0
    // 0.0, 0.0, 1.0, 0.0
    // 0.0, 0.0, 0.0, 1.0
    // The first row will be in xd0 and xd2, the second in xd4 and xd6, and so on.
    asm(" \
        ! Set up the three clearing patterns in the below-documented manner.\n \
        fldi0 fr0\n \
        fldi0 fr1\n \
        fldi1 fr2\n \
        fldi0 fr3\n \
        fldi0 fr4\n \
        fldi1 fr5\n \
        fschg\n \
        ! Use doubles to move more efficiently.\n \
        ! dr0 contains 0.0, 0.0\n \
        ! dr2 contains 1.0, 0.0\n \
        ! dr4 contains 0.0, 1.0\n \
        fmov dr2,xd0\n \
        fmov dr0,xd2\n \
        fmov dr4,xd4\n \
        fmov dr0,xd6\n \
        fmov dr0,xd8\n \
        fmov dr2,xd10\n \
        fmov dr0,xd12\n \
        fmov dr4,xd14\n \
        ! Now, return to caller.\n \
        fschg\n \
        " :
        /* No ouputs */ :
        /* No inputs */ :
        "fr0", "fr1", "fr2", "fr3", "fr4", "fr5"
    );

    irq_restore(old_irq);
}

void matrix_init_perspective(float fovy, float zNear, float zFar)
{
    // Actually set up the system matrix as such.
    if (video_is_vertical())
    {
        // Adjust coordinates so that the screen goes from -1.0 to 1.0 in both x and y direction.
        float halfheight = video_width() / 2.0;
        float halfwidth = video_height() / 2.0;
        matrix_t screenview_matrix = {
            halfwidth, 0.0, 0.0, 0.0,
            0.0, halfheight, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            halfwidth, halfheight, 0.0, 1.0
        };

        // Create a projection matrix which allows for perspective projection.
        float fovrads = (fovy / 180.0) * M_PI;
        float aspect = (float)halfheight / (float)halfwidth;
        float cot_fovy_2 = cos(fovrads / 2.0) / sin(fovrads / 2.0);
        matrix_t projection_matrix = {
            -cot_fovy_2 / aspect, 0.0, 0.0, 0.0,
            0.0, cot_fovy_2, 0.0, 0.0,
            0.0, 0.0, (zFar+zNear)/(zNear-zFar), -1.0,
            0.0, 0.0, 2*zFar*zNear/(zNear-zFar), 1.0
        };

        matrix_init_identity();
        matrix_apply(&screenview_matrix);
        matrix_rotate_z(-90.0);
        matrix_apply(&projection_matrix);
    }
    else
    {
        // Adjust coordinates so that the screen goes from -1.0 to 1.0 in both x and y direction.
        float halfwidth = video_width() / 2.0;
        float halfheight = video_height() / 2.0;
        matrix_t screenview_matrix = {
            halfwidth, 0.0, 0.0, 0.0,
            0.0, halfheight, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            halfwidth, halfheight, 0.0, 1.0
        };

        // Create a projection matrix which allows for perspective projection.
        float fovrads = (fovy / 180.0) * M_PI;
        float aspect = (float)halfwidth / (float)halfheight;
        float cot_fovy_2 = cos(fovrads / 2.0) / sin(fovrads / 2.0);
        matrix_t projection_matrix = {
            -cot_fovy_2 / aspect, 0.0, 0.0, 0.0,
            0.0, cot_fovy_2, 0.0, 0.0,
            0.0, 0.0, (zFar+zNear)/(zNear-zFar), -1.0,
            0.0, 0.0, 2*zFar*zNear/(zNear-zFar), 1.0
        };

        matrix_init_identity();
        matrix_apply(&screenview_matrix);
        matrix_apply(&projection_matrix);
    }
}

void matrix_apply(matrix_t *matrix)
{
    uint32_t old_irq = irq_disable();

    // Apply a 4x4 matrix in the input to the XMTRX accumulated viewport matrix.
    register matrix_t *matrix_param asm("r4") = matrix;
    asm(" \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fmov.s @r4+,fr3\n \
        ftrv xmtrx,fv0\n \
        fmov.s @r4+,fr4\n \
        fmov.s @r4+,fr5\n \
        fmov.s @r4+,fr6\n \
        fmov.s @r4+,fr7\n \
        ftrv xmtrx,fv4\n \
        fmov.s @r4+,fr8\n \
        fmov.s @r4+,fr9\n \
        fmov.s @r4+,fr10\n \
        fmov.s @r4+,fr11\n \
        ftrv xmtrx,fv8\n \
        fmov.s @r4+,fr12\n \
        fmov.s @r4+,fr13\n \
        fmov.s @r4+,fr14\n \
        fmov.s @r4+,fr15\n \
        ftrv xmtrx,fv12\n \
        frchg\n \
        " :
        /* No ouputs */ :
        "r" (matrix_param) :
        "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14", "fr15"
    );

    irq_restore(old_irq);
}

void matrix_set(matrix_t *matrix)
{
    uint32_t old_irq = irq_disable();

    // Set a 4x4 matrix into the XMTRX register.
    register matrix_t *matrix_param asm("r4") = matrix;
    asm(" \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fmov.s @r4+,fr3\n \
        fmov.s @r4+,fr4\n \
        fmov.s @r4+,fr5\n \
        fmov.s @r4+,fr6\n \
        fmov.s @r4+,fr7\n \
        fmov.s @r4+,fr8\n \
        fmov.s @r4+,fr9\n \
        fmov.s @r4+,fr10\n \
        fmov.s @r4+,fr11\n \
        fmov.s @r4+,fr12\n \
        fmov.s @r4+,fr13\n \
        fmov.s @r4+,fr14\n \
        fmov.s @r4+,fr15\n \
        frchg\n \
        " :
        /* No ouputs */ :
        "r" (matrix_param) :
        "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14", "fr15"
    );

    irq_restore(old_irq);
}

void matrix_get(matrix_t *matrix)
{
    uint32_t old_irq = irq_disable();

    // Set a 4x4 matrix into the XMTRX register.
    register matrix_t *matrix_param asm("r4") = matrix;
    asm(" \
        frchg\n \
        add #64,r4\n \
        fmov.s fr15,@-r4\n \
        fmov.s fr14,@-r4\n \
        fmov.s fr13,@-r4\n \
        fmov.s fr12,@-r4\n \
        fmov.s fr11,@-r4\n \
        fmov.s fr10,@-r4\n \
        fmov.s fr9,@-r4\n \
        fmov.s fr8,@-r4\n \
        fmov.s fr7,@-r4\n \
        fmov.s fr6,@-r4\n \
        fmov.s fr5,@-r4\n \
        fmov.s fr4,@-r4\n \
        fmov.s fr3,@-r4\n \
        fmov.s fr2,@-r4\n \
        fmov.s fr1,@-r4\n \
        fmov.s fr0,@-r4\n \
        frchg\n \
        " :
        /* No ouputs */ :
        "r" (matrix_param) :
        "fr0", "fr1", "fr2", "fr3", "fr4", "fr5", "fr6", "fr7", "fr8", "fr9", "fr10", "fr11", "fr12", "fr13", "fr14", "fr15"
    );

    irq_restore(old_irq);
}

void matrix_push()
{
    if (matrixpos < MAX_MATRIXES)
    {
        matrix_get(&sysmatrix[matrixpos]);
        matrixpos++;
    }
}

void matrix_pop()
{
    if (matrixpos > 0)
    {
        matrixpos--;
        matrix_set(&sysmatrix[matrixpos]);
    }
}

void matrix_affine_transform_vertex(vertex_t *src, vertex_t *dest, int n)
{
    // Let's do some bounds checking!
    if (n <= 0) { return; }

    uint32_t old_irq = irq_disable();

    // Given a pre-set XMTRX (use matrix_clear() and matrix_apply() to get here),
    // multiply it by a set of points to transform them from world space to screen space.
    // These are extended to homogenous coordinates by assuming a "w" value of 1.0.
    register vertex_t *src_param asm("r4") = src;
    register vertex_t *dst_param asm("r5") = dest;
    register int n_param asm("r6") = n;
    asm(" \
    .affineloop:\n \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fldi1 fr3\n \
        ftrv xmtrx,fv0\n \
        dt r6\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fmov.s fr1,@r5\n \
        add #4,r5\n \
        fmov.s fr2,@r5\n \
        add #4,r5\n \
        bf/s .affineloop\n \
        nop\n \
        " :
        /* No outputs */ :
        "r" (src_param), "r" (dst_param), "r" (n_param) :
        "fr0", "fr1", "fr2", "fr3"
    );

    irq_restore(old_irq);
}

void matrix_perspective_transform_vertex(vertex_t *src, vertex_t *dest, int n)
{
    // Let's do some bounds checking!
    if (n <= 0) { return; }

    uint32_t old_irq = irq_disable();

    // Given a pre-set XMTRX (use matrix_clear() and matrix_apply() to get here),
    // multiply it by a set of points to transform them from world space to screen space.
    // These are extended to homogenous coordinates by assuming a "w" value of 1.0.
    register vertex_t *src_param asm("r4") = src;
    register vertex_t *dst_param asm("r5") = dest;
    register int n_param asm("r6") = n;
    asm(" \
    .perspectiveloop:\n \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fldi1 fr3\n \
        ftrv xmtrx,fv0\n \
        dt r6\n \
        fdiv fr3,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fdiv fr3,fr1\n \
        fmov.s fr1,@r5\n \
        add #4,r5\n \
        fdiv fr3,fr2\n \
        fmov.s fr2,@r5\n \
        add #4,r5\n \
        bf/s .perspectiveloop\n \
        nop\n \
        " :
        /* No outputs */ :
        "r" (src_param), "r" (dst_param), "r" (n_param) :
        "fr0", "fr1", "fr2", "fr3"
    );

    irq_restore(old_irq);
}

void matrix_affine_transform_textured_vertex(textured_vertex_t *src, textured_vertex_t *dest, int n)
{
    // Let's do some bounds checking!
    if (n <= 0) { return; }

    uint32_t old_irq = irq_disable();

    // Given a pre-set XMTRX (use matrix_clear() and matrix_apply() to get here),
    // multiply it by a set of points to transform them from world space to screen space.
    // These are extended to homogenous coordinates by assuming a "w" value of 1.0.
    register textured_vertex_t *src_param asm("r4") = src;
    register textured_vertex_t *dst_param asm("r5") = dest;
    register int n_param asm("r6") = n;
    asm(" \
    .affinetexloop:\n \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fldi1 fr3\n \
        ftrv xmtrx,fv0\n \
        dt r6\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fmov.s fr1,@r5\n \
        add #4,r5\n \
        fmov.s fr2,@r5\n \
        add #4,r5\n \
        fmov.s @r4+,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fmov.s @r4+,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        bf/s .affinetexloop\n \
        nop\n \
        " :
        /* No outputs */ :
        "r" (src_param), "r" (dst_param), "r" (n_param) :
        "fr0", "fr1", "fr2", "fr3"
    );

    irq_restore(old_irq);
}

void matrix_perspective_transform_textured_vertex(textured_vertex_t *src, textured_vertex_t *dest, int n)
{
    // Let's do some bounds checking!
    if (n <= 0) { return; }

    uint32_t old_irq = irq_disable();

    // Given a pre-set XMTRX (use matrix_clear() and matrix_apply() to get here),
    // multiply it by a set of points to transform them from world space to screen space.
    // These are extended to homogenous coordinates by assuming a "w" value of 1.0.
    register textured_vertex_t *src_param asm("r4") = src;
    register textured_vertex_t *dst_param asm("r5") = dest;
    register int n_param asm("r6") = n;
    asm(" \
    .perspectivetexloop:\n \
        fmov.s @r4+,fr0\n \
        fmov.s @r4+,fr1\n \
        fmov.s @r4+,fr2\n \
        fldi1 fr3\n \
        ftrv xmtrx,fv0\n \
        dt r6\n \
        fdiv fr3,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fdiv fr3,fr1\n \
        fmov.s fr1,@r5\n \
        add #4,r5\n \
        fdiv fr3,fr2\n \
        fmov.s fr2,@r5\n \
        add #4,r5\n \
        fmov.s @r4+,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        fmov.s @r4+,fr0\n \
        fmov.s fr0,@r5\n \
        add #4,r5\n \
        bf/s .perspectivetexloop\n \
        nop\n \
        " :
        /* No outputs */ :
        "r" (src_param), "r" (dst_param), "r" (n_param) :
        "fr0", "fr1", "fr2", "fr3"
    );

    irq_restore(old_irq);
}

void matrix_rotate_x(float degrees)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a22 = matrix.a33 = cos((degrees / 180.0) * M_PI);
    matrix.a23 = -(matrix.a32 = sin((degrees / 180.0) * M_PI));
    matrix_apply(&matrix);
}

void matrix_rotate_y(float degrees)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a11 = matrix.a33 = cos((degrees / 180.0) * M_PI);
    matrix.a31 = -(matrix.a13 = sin((degrees / 180.0) * M_PI));
    matrix_apply(&matrix);
}

void matrix_rotate_z(float degrees)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a11 = matrix.a22 = cos((degrees / 180.0) * M_PI);
    matrix.a12 = -(matrix.a21 = sin((degrees / 180.0) * M_PI));
    matrix_apply(&matrix);
}

void matrix_scale_x(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a11 = amount;
    matrix_apply(&matrix);
}

void matrix_scale_y(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a22 = amount;
    matrix_apply(&matrix);
}

void matrix_scale_z(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a33 = amount;
    matrix_apply(&matrix);
}

void matrix_translate_x(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a41 = amount;
    matrix_apply(&matrix);
}

void matrix_translate_y(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a42 = amount;
    matrix_apply(&matrix);
}

void matrix_translate_z(float amount)
{
    matrix_t matrix = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    matrix.a43 = amount;
    matrix_apply(&matrix);
}
