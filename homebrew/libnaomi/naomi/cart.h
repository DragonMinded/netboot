#ifndef __CART_H
#define __CART_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Read len bytes from cartridge offset src into destination address dst.
// Note that dst and src both MUST be aligned to 2 bytes, and len must be
// a multiple of 2.
void cart_read(void *dst, uint32_t src, unsigned int len);

// Write len bytes to cartridge offset dst from source address src. Note
// that dst and src both MUST be aligned to 2 bytes, and len must be a
// multiple of 2.
void cart_write(uint32_t dst, void *src, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
