// vim: set fileencoding=utf-8
#include <stdlib.h>
#include "naomi/matrix.h"

void test_matrix_get_set(test_context_t *context)
{
    matrix_t mtrx;
    matrix_init_identity();
    matrix_get(&mtrx);

    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(mtrx, y, x) == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", matrix_index(mtrx, y, x), y, x);
    }

    matrix_t newmtrx;
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        matrix_index(newmtrx, y, x) = (float)i;
    }

    matrix_set(&newmtrx);
    matrix_get(&mtrx);

    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(mtrx, y, x) == (float)i, "Expected different value than %f for [%d][%d]!", matrix_index(mtrx, y, x), y, x);
    }
}

void test_matrix_push_pop(test_context_t *context)
{
    matrix_t mtrx1 = {
        1.0, 2.0, 3.0, 4.0,
        11.0, 12.0, 13.0, 14.0,
        21.0, 22.0, 23.0, 24.0,
        31.0, 32.0, 33.0, 34.0
    };
    matrix_t mtrx2 = {
        101.0, 102.0, 103.0, 104.0,
        111.0, 112.0, 113.0, 114.0,
        121.0, 122.0, 123.0, 124.0,
        131.0, 132.0, 133.0, 134.0
    };

    matrix_set(&mtrx1);
    matrix_push();
    matrix_set(&mtrx2);
    matrix_push();
    matrix_init_identity();

    matrix_t mtrx3;
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(mtrx3, y, x) == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", matrix_index(mtrx3, y, x), y, x);
    }

    matrix_pop();
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(mtrx3, y, x) == matrix_index(mtrx2, y, x), "Expected value %f but got %f for [%d][%d]!", matrix_index(mtrx2, y, x), matrix_index(mtrx3, y, x), y, x);
    }

    matrix_pop();
    matrix_get(&mtrx3);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(mtrx3, y, x) == matrix_index(mtrx1, y, x), "Expected value %f but got %f for [%d][%d]!", matrix_index(mtrx1, y, x), matrix_index(mtrx3, y, x), y, x);
    }
}

void test_matrix_apply(test_context_t *context)
{
    matrix_t mtrx1 = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    };

    // First test applying the identity matrix, which should leave us unchanged.
    matrix_init_identity();
    matrix_apply(&mtrx1);

    matrix_t result;
    matrix_get(&result);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(result, y, x) == ((x == y) ? 1.0 : 0.0), "Expected different value than %f for [%d][%d]!", matrix_index(result, y, x), y, x);
    }

    // Now, test multiplying two arbitrary matrixes.
    matrix_t matrixa = {
        3.0, 10.0, 12.0, 18.0,
        12.0, 1.0, 4.0, 9.0,
        9.0, 10.0, 12.0, 2.0,
        3.0, 12.0, 4.0, 10.0
    };
    matrix_t matrixb = {
        5.0, 7.0, 9.0, 10.0,
        2.0, 3.0, 3.0, 8.0,
        8.0, 10.0, 2.0, 3.0,
        3.0, 3.0, 4.0, 8.0
    };
    matrix_t matrixc = {
        210.0, 267.0, 236.0, 271.0,
        93.0, 149.0, 104.0, 149.0,
        171.0, 146.0, 172.0, 268.0,
        105.0, 169.0, 128.0, 169.0
    };

    matrix_set(&matrixa);
    matrix_apply(&matrixb);
    matrix_get(&result);
    for (int i = 0; i < 16; i++)
    {
        int x = i % 4;
        int y = i / 4;

        ASSERT(matrix_index(result, y, x) == matrix_index(matrixc, y, x), "Expected value %f but got %f for [%d][%d]!", matrix_index(matrixc, y, x), matrix_index(result, y, x), y, x);
    }
}

void test_matrix_affine_transform(test_context_t *context)
{
    matrix_init_identity();
    matrix_translate_x(10.0);
    matrix_translate_y(-20.0);
    matrix_translate_z(30.0);

    vertex_t coords[3] = {
        { 0.0, 0.0, 0.0 },
        { 10.0, 10.0, 10.0 },
        { -30.0, -30.0, -30.0 },
    };
    vertex_t newcoords[3];

    matrix_affine_transform_vertex(coords, newcoords, 3);

    vertex_t expected[3] = {
        { 10.0, -20.0, 30.0 },
        { 20.0, -10.0, 40.0 },
        { -20.0, -50.0, 0.0 },
    };

    for (unsigned int set = 0; set < 3; set++)
    {
        ASSERT(
            expected[set].x == newcoords[set].x,
            "Expected %f but got %f for coordinate %d x!",
            expected[set].x,
            newcoords[set].x,
            set
        );
        ASSERT(
            expected[set].y == newcoords[set].y,
            "Expected %f but got %f for coordinate %d y!",
            expected[set].y,
            newcoords[set].y,
            set
        );
        ASSERT(
            expected[set].z == newcoords[set].z,
            "Expected %f but got %f for coordinate %d z!",
            expected[set].z,
            newcoords[set].z,
            set
        );
    }
}

void test_matrix_affine_uv_transform(test_context_t *context)
{
    matrix_init_identity();
    matrix_translate_x(10.0);
    matrix_translate_y(-20.0);
    matrix_translate_z(30.0);

    textured_vertex_t coords[3] = {
        { 0.0, 0.0, 0.0, 0.0, 0.0 },
        { 10.0, 10.0, 10.0, 1.0, 1.0 },
        { -30.0, -30.0, -30.0, 2.0, 2.0 },
    };
    textured_vertex_t newcoords[3];

    matrix_affine_transform_textured_vertex(coords, newcoords, 3);

    textured_vertex_t expected[3] = {
        { 10.0, -20.0, 30.0, 0.0, 0.0 },
        { 20.0, -10.0, 40.0, 1.0, 1.0 },
        { -20.0, -50.0, 0.0, 2.0, 2.0 },
    };

    for (unsigned int set = 0; set < 3; set++)
    {
        ASSERT(
            expected[set].x == newcoords[set].x,
            "Expected %f but got %f for coordinate %d x!",
            expected[set].x,
            newcoords[set].x,
            set
        );
        ASSERT(
            expected[set].y == newcoords[set].y,
            "Expected %f but got %f for coordinate %d y!",
            expected[set].y,
            newcoords[set].y,
            set
        );
        ASSERT(
            expected[set].z == newcoords[set].z,
            "Expected %f but got %f for coordinate %d z!",
            expected[set].z,
            newcoords[set].z,
            set
        );
    }
}
