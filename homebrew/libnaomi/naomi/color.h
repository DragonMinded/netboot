#ifndef __COLOR_H
#define __COLOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Defines a color type, independent of the video mode.
typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} color_t;

// Generates a color suitable for passing into any function that
// requests a color parameter.
color_t rgb(unsigned int r, unsigned int g, unsigned int b);
color_t rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a);

#ifdef __cplusplus
}
#endif

#endif
