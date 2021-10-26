#ifndef __PACKET_H
#define __PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_OUTSTANDING_PACKETS 268
#define MAX_PACKET_LENGTH 253

typedef struct
{
    uint8_t data[MAX_PACKET_LENGTH];
    unsigned int len;
} packet_t;

typedef struct
{
    unsigned int packets_sent;
    unsigned int packets_received;
    unsigned int packets_cancelled;
    unsigned int checksum_errors;
    unsigned int packets_pending_send;
    unsigned int packets_pending_receive;
    unsigned int send_in_progress;
    unsigned int receive_in_progress;
} packetlib_stats_t;

void packetlib_init();
void packetlib_free();

int packetlib_send(void *data, unsigned int length);
int packetlib_recv(void *data, unsigned int *length);
void *packetlib_peek(int packetno, unsigned int *length);
void packetlib_discard(int packetno);

packetlib_stats_t packetlib_stats();
void packetlib_render_stats(char *buffer);

#ifdef __cplusplus
}
#endif

#endif
