#include <stdio.h>
#include <stdarg.h>
#include <zlib.h>
#include "common.h"
#include "message.h"

void host_printf(char *msg, ...)
{
    static char buffer[2048];

    if (msg)
    {
        va_list args;
        int length;
        va_start(args, msg);
        length = vsnprintf(buffer, 2047, msg, args);
        va_end(args);

        if (length > 0)
        {
            message_send(MESSAGE_HOST_PRINT, buffer, min(length, 2047));
        }
    }
}

int zlib_decompress(uint8_t *compressed, unsigned int compressedlen, uint8_t *decompressed, unsigned int decompressedlen)
{
    int ret;
    z_stream strm;

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = compressedlen;
    strm.next_in = compressed;
    strm.avail_out = decompressedlen;
    strm.next_out = decompressed;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
    {
        return -1;
    }

    ret = inflate(&strm, Z_NO_FLUSH);
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? 0 : -2;
}
