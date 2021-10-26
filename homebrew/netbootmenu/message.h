#ifndef __MESSAGE_H
#define __MESSAGE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MESSAGE_SELECTION 0x1000
#define MESSAGE_LOAD_SETTINGS 0x1001
#define MESSAGE_LOAD_SETTINGS_ACK 0x1002
#define MESSAGE_SAVE_CONFIG 0x1003
#define MESSAGE_SAVE_CONFIG_ACK 0x1004

int message_send(uint16_t type, void * data, unsigned int length);
int message_recv(uint16_t *type, void ** data, unsigned int *length);

#ifdef __cplusplus
}
#endif

#endif
