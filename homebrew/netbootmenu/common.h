#ifndef __COMMON_H
#define __COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

void host_printf(char *msg, ...);

int zlib_decompress(uint8_t *compressed, unsigned int compressedlen, uint8_t *decompressed, unsigned int decompressedlen);

#ifdef __cplusplus
}
#endif

#endif
