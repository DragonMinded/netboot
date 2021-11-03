#ifndef __PACKET_H
#define __PACKET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAX_OUTSTANDING_PACKETS 256
#define MAX_PACKET_LENGTH ((0xFF - 2) * 3)

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

// Initialize or free the packetlib library. Note that this will
// install a DIMM communication hook, so be sure that you aren't
// using any other DIMM communication callbacks.
void packetlib_init();
void packetlib_free();

// Set a config register that can be read by the host to determine
// current capabilities.
#define CONFIG_MESSAGE_EXISTS 0x00000001
#define CONFIG_MESSAGE_HAS_ZLIB 0x00000002

void packetlib_set_config(uint32_t config_mask);

// Send a single packet with at least 1 byte of data and at most
// MAX_PACKET_LENGTH bytes of data. Returns 0 on success or a negative
// number indicating failure.
int packetlib_send(void *data, unsigned int length);

// Receive a single packet. Returns 0 on success or a negative number
// indicating failure. Note that having no packets to receive is considered
// a failure.
int packetlib_recv(void *data, unsigned int *length);

// Peek at a particular packet (Betweeen 0 and MAX_OUTSTANDING_PACKETS)
// but do not consider it received. If there is a packet in that slot
// then a pointer to the data is returned and length is set.
void *packetlib_peek(int packetno, unsigned int *length);

// Discard a packet that was peeked at but not recv'd. Takes the same
// packet range as packetlib_peek().
void packetlib_discard(int packetno);

// Read or write one of two 32-bit scratch registers, available to be
// written to or read from on the host side as well. Use this for
// communicating small bits of data that do not deserve their own packet.
void packetlib_write_scratch1(uint32_t data);
void packetlib_write_scratch2(uint32_t data);
uint32_t packetlib_read_scratch1();
uint32_t packetlib_read_scratch2();

// Grab statistics about the packetlib execution.
packetlib_stats_t packetlib_stats();

// Render statistics to a buffer that can be displayed for debugging
// purposes. Calls packetlib_stats() under the hood and renders the
// statistics in an easy to view manner.
void packetlib_render_stats(char *buffer);

#ifdef __cplusplus
}
#endif

#endif
