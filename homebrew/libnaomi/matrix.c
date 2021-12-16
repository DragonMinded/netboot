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
        fschg\n \
        fmov dr0,xd0\n \
        fmov dr2,xd2\n \
        fmov dr4,xd4\n \
        fmov dr6,xd6\n \
        fmov dr8,xd8\n \
        fmov dr10,xd10\n \
        fmov dr12,xd12\n \
        fmov dr14,xd14\n \
        fschg\n \
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
        fschg\n \
        fmov dr0,xd0\n \
        fmov dr2,xd2\n \
        fmov dr4,xd4\n \
        fmov dr6,xd6\n \
        fmov dr8,xd8\n \
        fmov dr10,xd10\n \
        fmov dr12,xd12\n \
        fmov dr14,xd14\n \
        fschg\n \
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
        fschg\n \
        fmov xd0,dr0\n \
        fmov xd2,dr2\n \
        fmov xd4,dr4\n \
        fmov xd6,dr6\n \
        fmov xd8,dr8\n \
        fmov xd10,dr10\n \
        fmov xd12,dr12\n \
        fmov xd14,dr14\n \
        fschg\n \
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

float _minor(float m[16], int r0, int r1, int r2, int c0, int c1, int c2)
{
    return (
        m[4*r0+c0] * (m[4*r1+c1] * m[4*r2+c2] - m[4*r2+c1] * m[4*r1+c2]) -
        m[4*r0+c1] * (m[4*r1+c0] * m[4*r2+c2] - m[4*r2+c0] * m[4*r1+c2]) +
        m[4*r0+c2] * (m[4*r1+c0] * m[4*r2+c1] - m[4*r2+c0] * m[4*r1+c1])
    );
}

void _adjoint(float m[16], float adjOut[16])
{
    adjOut[0] = _minor(m,1,2,3,1,2,3);
    adjOut[1] = -_minor(m,0,2,3,1,2,3);
    adjOut[2] = _minor(m,0,1,3,1,2,3);
    adjOut[3] = -_minor(m,0,1,2,1,2,3);
    adjOut[4] = -_minor(m,1,2,3,0,2,3);
    adjOut[5] = _minor(m,0,2,3,0,2,3);
    adjOut[6] = -_minor(m,0,1,3,0,2,3);
    adjOut[7] = _minor(m,0,1,2,0,2,3);
    adjOut[8] = _minor(m,1,2,3,0,1,3);
    adjOut[9] = -_minor(m,0,2,3,0,1,3);
    adjOut[10] = _minor(m,0,1,3,0,1,3);
    adjOut[11] = -_minor(m,0,1,2,0,1,3);
    adjOut[12] = -_minor(m,1,2,3,0,1,2);
    adjOut[13] = _minor(m,0,2,3,0,1,2);
    adjOut[14] = -_minor(m,0,1,3,0,1,2);
    adjOut[15] = _minor(m,0,1,2,0,1,2);
}

float _det(float m[16])
{
    return (
        m[0] * _minor(m, 1, 2, 3, 1, 2, 3) -
        m[1] * _minor(m, 1, 2, 3, 0, 2, 3) +
        m[2] * _minor(m, 1, 2, 3, 0, 1, 3) -
        m[3] * _minor(m, 1, 2, 3, 0, 1, 2)
    );
}

void _invertRowMajor(float m[16], float invOut[16])
{
    _adjoint(m, invOut);

    float inv_det = 1.0f / _det(m);
    for(int i = 0; i < 16; i++)
    {
        invOut[i] = invOut[i] * inv_det;
    }
}

void matrix_invert()
{
    float orig[16];
    matrix_get((matrix_t *)orig);

    float upd[16];
    _invertRowMajor(orig, upd);
    matrix_set((matrix_t *)upd);
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
    .loop%=:\n \
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
        bf/s .loop%=\n \
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
    .loop%=:\n \
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
        bf/s .loop%=\n \
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

void matrix_rotate_origin_x(vertex_t *origin, float amount)
{
    matrix_t backagain = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    backagain.a41 = origin->x;
    backagain.a42 = origin->y;
    backagain.a43 = origin->z;
    matrix_apply(&backagain);
    matrix_rotate_x(amount);

    matrix_t there = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    there.a41 = -origin->x;
    there.a42 = -origin->y;
    there.a43 = -origin->z;
    matrix_apply(&there);
}

void matrix_rotate_origin_y(vertex_t *origin, float amount)
{
    matrix_t backagain = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    backagain.a41 = origin->x;
    backagain.a42 = origin->y;
    backagain.a43 = origin->z;
    matrix_apply(&backagain);
    matrix_rotate_y(amount);

    matrix_t there = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    there.a41 = -origin->x;
    there.a42 = -origin->y;
    there.a43 = -origin->z;
    matrix_apply(&there);
}

void matrix_rotate_origin_z(vertex_t *origin, float amount)
{
    matrix_t backagain = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    backagain.a41 = origin->x;
    backagain.a42 = origin->y;
    backagain.a43 = origin->z;
    matrix_apply(&backagain);
    matrix_rotate_z(amount);

    matrix_t there = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };
    there.a41 = -origin->x;
    there.a42 = -origin->y;
    there.a43 = -origin->z;
    matrix_apply(&there);
}
