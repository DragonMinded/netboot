#ifndef __UTF8_H
#define __UTF8_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// UTF-8 handling for unicode text.
unsigned int utf8_strlen(const char * const str);
uint32_t *utf8_convert(const char * const str);

#ifdef __cplusplus
}
#endif

#endif
