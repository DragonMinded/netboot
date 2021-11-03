#include <stdio.h>
#include <stdarg.h>
#include "naomi/message/message.h"
#include "common.h"

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
