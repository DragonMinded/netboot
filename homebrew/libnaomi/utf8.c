#include <stdlib.h>
#include <string.h>
#include "naomi/utf8.h"

unsigned int utf8_strlen(const char * const str)
{
    uint8_t *data = (uint8_t *)str;
    unsigned int max = strlen(str);
    unsigned int len = 0;
    unsigned int pos = 0;

    while (pos < max)
    {
        if ((data[pos] & 0x80) == 0)
        {
            len ++;
            pos ++;
        }
        else if((data[pos] & 0xE0) == 0xC0)
        {
            len ++;
            pos += 2;
        }
        else if((data[pos] & 0xF0) == 0xE0)
        {
            len ++;
            pos += 3;
        }
        else if((data[pos] & 0xF1) == 0xF0)
        {
            len ++;
            pos += 4;
        }
        else
        {
            // Error!
            return 0;
        }
    }

    return len;
}

uint32_t *utf8_convert(const char * const str)
{
    // First make some room for the output.
    unsigned int length = utf8_strlen(str);
    uint32_t *chars = malloc(sizeof(uint32_t) * (length + 1));
    if (chars == 0)
    {
        return 0;
    }
    memset(chars, 0, sizeof(uint32_t) * (length + 1));

    // Now, convert characters one at a time.
    uint8_t *data = (uint8_t *)str;
    unsigned int inpos = 0;
    unsigned int outpos = 0;

    while (outpos < length)
    {
        if ((data[inpos] & 0x80) == 0)
        {
            chars[outpos] = data[inpos] & 0x7F;

            inpos ++;
            outpos ++;
        }
        else if((data[inpos] & 0xE0) == 0xC0)
        {
            chars[outpos] = ((data[inpos] & 0x1F) << 6) | (data[inpos + 1] & 0x3F);

            inpos += 2;
            outpos ++;
        }
        else if((data[inpos] & 0xF0) == 0xE0)
        {
            chars[outpos] = ((data[inpos] & 0x0F) << 12) | ((data[inpos + 1] & 0x3F) << 6) | (data[inpos + 2] & 0x3F);

            inpos += 3;
            outpos ++;
        }
        else if((data[inpos] & 0xF1) == 0xF0)
        {
            chars[outpos] = ((data[inpos] & 0x03) << 18) | ((data[inpos + 1] & 0x3F) << 12) | ((data[inpos + 2] & 0x3F) << 6) | (data[inpos + 3] & 0x3F);

            inpos += 4;
            outpos ++;
        }
        else
        {
            // Error!
            break;
        }
    }

    return chars;
}
