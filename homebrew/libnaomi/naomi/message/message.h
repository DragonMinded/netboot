#ifndef __MESSAGE_H
#define __MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_MESSAGE_LENGTH 0xFFFF

// Send or receive an arbitrary message, tagged with a type. Note that the type
// can be any integer between 0x0000 and 0x7FFF. The data can be any length, or
// zero length. This takes care of packetizing up and compressing/decompressing
// the data. Before calling these, make sure that you call packetlib_init().
// Both functions will return 0 on success and a negative integer on failure.
// Note that for message_recv(), having no pending message is considered a failure.
int message_send(uint16_t type, void * data, unsigned int length);
int message_recv(uint16_t *type, void ** data, unsigned int *length);

// Set up stdin/stdout/stderr console redirection to a host program that will
// react accordingly. Note that this requires a host program that understands
// particular stdin/stdout/stderr messages. If such a program exists and is
// connected, then printf() and friends will appear on the host as debugging
// messages.
void message_stdio_redirect_init();
void message_stdio_redirect_free();

#ifdef __cplusplus
}
#endif

#endif
