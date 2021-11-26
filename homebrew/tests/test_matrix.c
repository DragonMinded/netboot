// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/matrix.h"

void test_matrix_get_set(test_context_t *context)
{
    float mtrx[4][4];

    matrix_init_identity();
    matrix_get(&mtrx);

    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(mtrx[y][x] == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", mtrx[y][x], y, x);
    }

    float newmtrx[4][4];
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        newmtrx[y][x] = (float)i;
    }

    matrix_set(&newmtrx);
    matrix_get(&mtrx);

    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(mtrx[y][x] == (float)i, "Expected different value than %f for [%d][%d]!", mtrx[y][x], y, x);
    }
}

void test_matrix_push_pop(test_context_t *context)
{
    float mtrx1[4][4] = {
        { 1.0, 2.0, 3.0, 4.0 },
        { 11.0, 12.0, 13.0, 14.0 },
        { 21.0, 22.0, 23.0, 24.0 },
        { 31.0, 32.0, 33.0, 34.0 },
    };
    float mtrx2[4][4] = {
        { 101.0, 102.0, 103.0, 104.0 },
        { 111.0, 112.0, 113.0, 114.0 },
        { 121.0, 122.0, 123.0, 124.0 },
        { 131.0, 132.0, 133.0, 134.0 },
    };

    matrix_set(&mtrx1);
    matrix_push();
    matrix_set(&mtrx2);
    matrix_push();
    matrix_init_identity();

    float mtrx3[4][4];
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(mtrx3[y][x] == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", mtrx3[y][x], y, x);
    }

    matrix_pop();
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(mtrx3[y][x] == mtrx2[y][x], "Expected value %f but got %f for [%d][%d]!", mtrx2[y][x], mtrx3[y][x], y, x);
    }

    matrix_pop();
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(mtrx3[y][x] == mtrx1[y][x], "Expected value %f but got %f for [%d][%d]!", mtrx1[y][x], mtrx3[y][x], y, x);
    }
}

void test_matrix_apply(test_context_t *context)
{
    float mtrx1[4][4] = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 1.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 },
    };

    // First test applying the identity matrix, which should leave us unchanged.
    matrix_init_identity();
    matrix_apply(&mtrx1);

    float result[4][4];
    matrix_get(&result);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(result[y][x] == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", result[y][x], y, x);
    }

    // Now, test multiplying two arbitrary matrixes.
    float matrixa[4][4] = {
        { 3.0, 10.0, 12.0, 18.0 },
        { 12.0, 1.0, 4.0, 9.0 },
        { 9.0, 10.0, 12.0, 2.0 },
        { 3.0, 12.0, 4.0, 10.0 },
    };
    float matrixb[4][4] = {
        { 5.0, 7.0, 9.0, 10.0 },
        { 2.0, 3.0, 3.0, 8.0 },
        { 8.0, 10.0, 2.0, 3.0 },
        { 3.0, 3.0, 4.0, 8.0 },
    };
    float matrixc[4][4] = {
        { 210.0, 267.0, 236.0, 271.0 },
        { 93.0, 149.0, 104.0, 149.0 },
        { 171.0, 146.0, 172.0, 268.0 },
        { 105.0, 169.0, 128.0, 169.0 },
    };

    matrix_set(&matrixa);
    matrix_apply(&matrixb);
    matrix_get(&result);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(result[y][x] == matrixc[y][x], "Expected value %f but got %f for [%d][%d]!", matrixc[y][x], result[y][x], y, x);
    }
}

void test_matrix_affine_transform(test_context_t *context)
{
    matrix_init_identity();
    matrix_translate_x(10.0);
    matrix_translate_y(-20.0);
    matrix_translate_z(30.0);

    float coords[3][3] = {
        { 0.0, 0.0, 0.0 },
        { 10.0, 10.0, 10.0 },
        { -30.0, -30.0, -30.0 },
    };
    float newcoords[3][3];

    matrix_affine_transform_coords(coords, newcoords, 3);

    float expected[3][3] = {
        { 10.0, -20.0, 30.0 },
        { 20.0, -10.0, 40.0 },
        { -20.0, -50.0, 0.0 },
    };

    for (unsigned int set = 0; set < 3; set++)
    {
        for (unsigned int which = 0; which < 3; which++)
        {
            ASSERT(
                expected[set][which] == newcoords[set][which],
                "Expected %f but got %f for coordinate %d %s!",
                expected[set][which],
                newcoords[set][which],
                set,
                which == 0 ? "x" : (which == 1 ? "y" : "z")
            );
        }
    }
}
