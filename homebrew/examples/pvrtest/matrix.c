#include <naomi/interrupt.h>
#include "matrix.h"

void clear_matrix()
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

void apply_matrix(float (*matrix)[4][4])
{
    uint32_t old_irq = irq_disable();

    // Apply a 4x4 matrix in the input to the XMTRX accumulated viewport matrix.
    register float (*matrix_param)[4][4] asm("r4") = matrix;
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

void transform_coords(float (*src)[3], float (*dest)[3], int n)
{
    // Let's do some bounds checking!
    if (n <= 0) { return; }

    uint32_t old_irq = irq_disable();

    // Given a pre-set XMTRX (use matrix_clear() and matrix_apply() to get here),
    // multiply it by a set of points to transform them from world space to screen space.
    // These are extended to homogenous coordinates by assuming a "w" value of 1.0.
    register float (*src_param)[3] asm("r4") = src;
    register float (*dst_param)[3] asm("r5") = dest;
    register int n_param asm("r6") = n;
    asm(" \
    .loop:\n \
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
        bf/s .loop\n \
        nop\n \
        " :
        /* No outputs */ :
        "r" (src_param), "r" (dst_param), "r" (n_param) :
        "fr0", "fr1", "fr2", "fr3" 
    );

    irq_restore(old_irq);
}
