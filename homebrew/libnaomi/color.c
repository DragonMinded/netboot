#include <stdint.h>
#include "naomi/color.h"

color_t rgb(unsigned int r, unsigned int g, unsigned int b)
{
    color_t color = { r, g, b, 255 };
    return color;
}

color_t rgba(unsigned int r, unsigned int g, unsigned int b, unsigned int a)
{
    color_t color = { r, g, b, a };
    return color;
}
