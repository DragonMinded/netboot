#ifndef __MATRIX_H
#define __MATRIX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Type definition for a 4x4 matrix.
typedef struct
{
    float a11;
    float a12;
    float a13;
    float a14;
    float a21;
    float a22;
    float a23;
    float a24;
    float a31;
    float a32;
    float a33;
    float a34;
    float a41;
    float a42;
    float a43;
    float a44;
} matrix_t;

// Dirty trick to make indexing programatically into matrixes possible.
#define matrix_index(matrix, row, col) (*((&(matrix).a11) + ((row) * 4) + (col)))

// Initialize the system matrix with a 4x4 identity matrix.
void matrix_init_identity();

// Initialize the system matrix with a 4x4 projection matrix.
void matrix_init_perspective(float fovy, float zNear, float zFar);

// Set a 4x4 matrix into the system matrix.
void matrix_set(matrix_t *matrix);

// Get the 4x4 system matrix and put it into the input parameter.
void matrix_get(matrix_t *matrix);

// Push the system matrix onto a saved matrix stack, to be popped later
// in order to restore it.
void matrix_push();

// Pop a previously stored system matrix off the stack and place it into
// the system matrix.
void matrix_pop();

// Apply another 4x4 matrix to the system matrix by multiplying it.
void matrix_apply(matrix_t * matrix);

// Rotate the system matrix about a given axis and given degrees, where 0.0 is identity rotation.
void matrix_rotate_x(float degrees);
void matrix_rotate_y(float degrees);
void matrix_rotate_z(float degrees);

// Scale the system matrix on a given axis and given amount, where 1.0 is identity scale.
void matrix_scale_x(float amount);
void matrix_scale_y(float amount);
void matrix_scale_z(float amount);

// Translate the system matrix on a given axis and given amount, where 0.0 is identity translation.
void matrix_translate_x(float amount);
void matrix_translate_y(float amount);
void matrix_translate_z(float amount);

// Type definitions for both regular vertexes as well as vertexes that also hold texture information.
typedef struct
{
    float x;
    float y;
    float z;
} vertex_t;

typedef struct
{
    float x;
    float y;
    float z;
    void *texture;
    float u;
    float v;
} textured_vertex_t;

// Transform a series of x, y, z coordinates from one worldspace to another (useful for
// rotating/transforming/scaling objects in worldspace) by extending them to homogenous
// coordinates and then multiplying them by the system matrix. Note that this does not
// divide the resulting coordinates by w and it is simply discarded, so this should not
// be used to calcualte final screen coordinates of some coordinate list.
void matrix_affine_transform_vertex(vertex_t *src, vertex_t *dest, int n);

// Transform a series of x, y, z coordinates from worldspace to system matrix space
// by extending them to homogenous coordinates and then multiplying them by the
// system matrix. Note that this also divides the resulting x, y, z coordinates by
// the extended w coordinate as required for submitting vertexes to the TA/PVR.
void matrix_perspective_transform_vertex(vertex_t *src, vertex_t *dest, int n);

#ifdef __cplusplus
}
#endif

#endif
