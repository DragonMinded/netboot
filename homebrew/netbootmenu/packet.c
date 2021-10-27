#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/dimmcomms.h"
#include "common.h"
#include "packet.h"

typedef struct
{
    packet_t *pending_packets[MAX_OUTSTANDING_PACKETS];
    packet_t *received_packets[MAX_OUTSTANDING_PACKETS];
    uint8_t pending_send_data[MAX_PACKET_LENGTH];
    int pending_send_size;
    int pending_send_location;
    uint8_t pending_recv_data[MAX_PACKET_LENGTH];
    int pending_recv_size;
    int pending_recv_location;
    unsigned int success_sent;
    unsigned int success_received;
    unsigned int cancelled_packets;
    unsigned int checksum_errors;
    uint32_t scratch1;
    uint32_t scratch2;
} packetlib_state_t;

static packetlib_state_t packetlib_state;

// Forward definitions for stuff that needs to be both before and after other functions.
uint32_t peek_memory(unsigned int address, int size);
void poke_memory(unsigned int address, int size, uint32_t data);

void packetlib_init()
{
    // Initialize packet library.
    for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i++) {
        packetlib_state.pending_packets[i] = 0;
        packetlib_state.received_packets[i] = 0;
    }

    packetlib_state.pending_send_size = 0;
    packetlib_state.pending_recv_size = 0;
    packetlib_state.success_sent = 0;
    packetlib_state.success_received = 0;
    packetlib_state.cancelled_packets = 0;
    packetlib_state.checksum_errors = 0;
    packetlib_state.scratch1 = 0;
    packetlib_state.scratch2 = 0;

    // Attach our handlers for sending/receiving data.
    dimm_comms_attach_hooks(&peek_memory, &poke_memory);
}

void packetlib_free()
{
    // No more receiving messages.
    dimm_comms_detach_hooks();

    // Free any outstanding packets.
    for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i++) {
        if (packetlib_state.pending_packets[i] != 0) {
            free(packetlib_state.pending_packets[i]);
            packetlib_state.pending_packets[i] = 0;
        }
        if (packetlib_state.received_packets[i] == 0) {
            free(packetlib_state.received_packets[i]);
            packetlib_state.received_packets[i] = 0;
        }
    }
}

packetlib_stats_t packetlib_stats()
{
    packetlib_stats_t stats;

    stats.packets_sent = packetlib_state.success_sent;
    stats.packets_received = packetlib_state.success_received;
    stats.packets_cancelled = packetlib_state.cancelled_packets;
    stats.checksum_errors = packetlib_state.checksum_errors;
    stats.packets_pending_send = 0;
    stats.packets_pending_receive = 0;
    for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i++) {
        if (packetlib_state.pending_packets[i] != 0) {
            stats.packets_pending_send++;
        }
        if (packetlib_state.received_packets[i] != 0) {
            stats.packets_pending_receive++;
        }
    }
    stats.send_in_progress = packetlib_state.pending_send_size > 0 ? 1 : 0;
    stats.receive_in_progress = packetlib_state.pending_recv_size > 0 ? 1 : 0;
    return stats;
}

int packetlib_send(void *data, unsigned int length)
{
    if (length == 0 || length > MAX_PACKET_LENGTH)
    {
        return -1;
    }

    for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i ++)
    {
        if (packetlib_state.pending_packets[i] == 0)
        {
            packetlib_state.pending_packets[i] = (packet_t *)malloc(sizeof(packet_t));
            memcpy(packetlib_state.pending_packets[i]->data, data, length);
            packetlib_state.pending_packets[i]->len = length;
            return 0;
        }
    }

    return -2;
}

int packetlib_recv(void *data, unsigned int *length)
{
    for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i ++)
    {
        if (packetlib_state.received_packets[i] != 0)
        {
            // Copy the data over.
            memcpy(data, packetlib_state.received_packets[i]->data, packetlib_state.received_packets[i]->len);
            *length = packetlib_state.received_packets[i]->len;

            // Free up the packet for later use.
            free(packetlib_state.received_packets[i]);
            packetlib_state.received_packets[i] = 0;

            // Success!
            return 0;
        }
    }

    return -1;
}

void *packetlib_peek(int packetno, unsigned int *length)
{
    if (packetlib_state.received_packets[packetno] != 0)
    {
        *length = packetlib_state.received_packets[packetno]->len;
        return packetlib_state.received_packets[packetno]->data;
    }
    else
    {
        *length = 0;
        return 0;
    }
}

void packetlib_discard(int packetno)
{
    if (packetlib_state.received_packets[packetno] != 0)
    {
        // Free up the packet for later use.
        free(packetlib_state.received_packets[packetno]);
        packetlib_state.received_packets[packetno] = 0;
    }
}

uint32_t checksum_add(uint32_t value)
{
    uint8_t sum = ((value & 0xFF) + ((value >> 8) & 0xFF)) & 0xFF;
    return (((~sum) & 0xFF) << 16) | (value & 0x0000FFFF);
}

int checksum_verify(uint32_t value)
{
    uint8_t sum = ((value & 0xFF) + ((value >> 8) & 0xFF)) & 0xFF;
    return (((~sum) & 0xFF) == ((value >> 16) & 0xFF)) ? 1 : 0;
}

uint32_t read_data()
{
    // If we have no data to send, bail out.
    if (packetlib_state.pending_send_size == 0)
    {
        return 0;
    }
    if (packetlib_state.pending_send_size == packetlib_state.pending_send_location)
    {
        return 0;
    }

    // First, construct the location portion of the packet.
    uint32_t response = ((packetlib_state.pending_send_location + 1) << 24) & 0xFF000000;

    // Now, until we run out of data, stick more into the buffer.
    for (int i = 16; i >= 0; i -= 8)
    {
        if (packetlib_state.pending_send_size == packetlib_state.pending_send_location)
        {
            return response;
        }

        response |= (packetlib_state.pending_send_data[packetlib_state.pending_send_location++] & 0xFF) << i;
    }

    return response;
}

uint32_t read_send_status()
{
    // Read the send status register for our communications protocol.
    // Top 8 bits is all 0s to make sure that it doesn't get confused
    // with a data send/receive which has a top byte that can only be
    // a nonzero value from 1-254. The next 8 bits are a simple inverted
    // checksum of the rest of the packet. Next 8 bits is the size of
    // a pending packet to send from naomi to host. Next 8 bits is the
    // location of the send in progress.
    uint32_t regdata = 0;

    if (packetlib_state.pending_send_size == 0)
    {
        // Attempt to find another packet to send.
        for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i ++)
        {
            if (packetlib_state.pending_packets[i] != 0)
            {
                // Grab the pending packet information, set up the transfer.
                memcpy(packetlib_state.pending_send_data, packetlib_state.pending_packets[i]->data, packetlib_state.pending_packets[i]->len);
                packetlib_state.pending_send_size = packetlib_state.pending_packets[i]->len;
                packetlib_state.pending_send_location = 0;

                // Get rid of the packet on the pending packets buffer.
                free(packetlib_state.pending_packets[i]);
                packetlib_state.pending_packets[i] = 0;
                break;
            }
        }
    }

    if (packetlib_state.pending_send_size != 0)
    {
        regdata = ((packetlib_state.pending_send_size << 8) & 0xFF00) | (packetlib_state.pending_send_location & 0x00FF);
    }

    // Actually return the data.
    return checksum_add(regdata);
}

void write_send_status(uint32_t status)
{
    // Write the send status register for our communications protocol. The only
    // things that the host is allowed to modify is the current location, so it
    // can rewind for missed data. It can also acknowledge the transfer by setting
    // the current location to the length of the packet.
    if (checksum_verify(status))
    {
        unsigned int location = status & 0xFF;
        if (location < packetlib_state.pending_send_size)
        {
            // Host is requesting a resend of some data.
            packetlib_state.pending_send_location = location;
        }
        else if(location == packetlib_state.pending_send_size)
        {
            // Transfer succeeded! Get rid of the current pending transfer.
            packetlib_state.pending_send_size = 0;
            packetlib_state.pending_send_location = 0;
            packetlib_state.success_sent ++;
        }
    }
    else
    {
        packetlib_state.checksum_errors ++;
    }
}

void write_data(uint32_t data)
{
    // Much like sending data to the host, the top byte is the location + 1 (so it
    // can never be 0x00 or 0xFF, two values commonly seen when the net dimm firmware
    // fails to read a packet or reads a copy of another register), then the next
    // three bytes are optionally the packet data. Unlike the host which can reassemble
    // packets in any order, we aren't so powerful. We simply check to see if the location
    // is where we left off. If it is, we accept thet packet. If not, then we ignore it.
    // The host is responsible for checking the receive status register after transferring
    // to see if it needs to rewind, or if the transfer succeeded.
    if (packetlib_state.pending_recv_size != 0)
    {
        unsigned int location = ((data >> 24) & 0xFF);
        if (location == 0xFF || location == 0x00)
        {
            // This is a bogus packet.
            return;
        }

        // Get the actual location.
        location -= 1;
        if (location != packetlib_state.pending_recv_location)
        {
            // We missed some data.
            return;
        }

        // Copy data until we have no more data left to copy, or until we hit the end of
        // the packet. If we hit the end of the packet, acknowledge the successful receipt
        // by setting the current location and size to zero.
        for (int i = 16; i >= 0; i -= 8)
        {
            packetlib_state.pending_recv_data[packetlib_state.pending_recv_location++] = (data >> i) & 0xFF;

            if (packetlib_state.pending_recv_size == packetlib_state.pending_recv_location)
            {
                // We did it! Add to the pending receive queue.
                for (int j = 0; j < MAX_OUTSTANDING_PACKETS; j ++)
                {
                    if (packetlib_state.received_packets[j] == 0)
                    {
                        // Copy the packet information so userspace can read it.
                        packetlib_state.received_packets[j] = (packet_t *)malloc(sizeof(packet_t));
                        memcpy(packetlib_state.received_packets[j]->data, packetlib_state.pending_recv_data, packetlib_state.pending_recv_size);
                        packetlib_state.received_packets[j]->len = packetlib_state.pending_recv_size;
                        break;
                    }
                }

                // Mark that the packet was received.
                packetlib_state.pending_recv_size = 0;
                packetlib_state.pending_recv_location = 0;
                packetlib_state.success_received ++;

                return;
            }
        }
    }
}

uint32_t read_recv_status()
{
    // Read the receive status register. This is a carbon copy of the read_send_status
    // register except for the values are for receiving a packet from the host instead
    // of sending a packet to the host.
    uint32_t regdata = 0;

    if (packetlib_state.pending_recv_size != 0)
    {
        regdata = ((packetlib_state.pending_recv_size << 8) & 0xFF00) | (packetlib_state.pending_recv_location & 0x00FF);
    }

    // Actually return the data.
    return checksum_add(regdata);
}

void write_recv_status(uint32_t status)
{
    // Write the receive status register. This is similar to write_send_status in that
    // the host is allowed to send the length to initiate a transfer. However, it should
    // only do so if the length is currently 0, and it can only change the length from
    // 0 to some packet length to be received. It is responsible for checking the current
    // location to see if data needs to be rewound, and if it is sending a packet and the
    // length goes back to 0 it means that the packet has been received successfully. The
    // host does not have access to change the location. If the host determines that a
    // previous transfer was mid-way through and it does not have knowledge of it, then
    // it should cancel the transfer by writing all 0's to this register.
    if (checksum_verify(status))
    {
        unsigned int size = (status >> 8) & 0xFF;
        if (size > 0 && size <= MAX_PACKET_LENGTH)
        {
            if (packetlib_state.pending_recv_size == 0)
            {
                // Start a new transfer, but only if we have room in our receive queue.
                for (int i = 0; i < MAX_OUTSTANDING_PACKETS; i++)
                {
                    if (packetlib_state.received_packets[i] == 0)
                    {
                        packetlib_state.pending_recv_size = size;
                        packetlib_state.pending_recv_location = 0;
                        return;
                    }
                }
            }
        }
        else if (size == 0)
        {
            // Cancel any pending transfer.
            if (packetlib_state.pending_recv_size != 0)
            {
                packetlib_state.pending_recv_size = 0;
                packetlib_state.pending_recv_location = 0;
                packetlib_state.cancelled_packets ++;
            }
        }
    }
    else
    {
        packetlib_state.checksum_errors ++;
    }
}

void packetlib_write_scratch1(uint32_t data)
{
    packetlib_state.scratch1 = data;
}

void packetlib_write_scratch2(uint32_t data)
{
    packetlib_state.scratch2 = data;
}

uint32_t packetlib_read_scratch1()
{
    return packetlib_state.scratch1;
}

uint32_t packetlib_read_scratch2()
{
    return packetlib_state.scratch2;
}

uint32_t peek_memory(unsigned int address, int size)
{
    if (size == 4)
    {
        if ((address & 0xFFFFFF) == 0xC0DE10) {
            // Read data register.
            return read_data();
        }
        else if ((address & 0xFFFFFF) == 0xC0DE20) {
            // Read status register.
            return read_send_status();
        }
        else if ((address & 0xFFFFFF) == 0xC0DE30) {
            // Read status register.
            return read_recv_status();
        }
        else if ((address & 0xFFFFFF) == 0xC0DE40) {
            // Read scratch register 1.
            return packetlib_read_scratch1();
        }
        else if ((address & 0xFFFFFF) == 0xC0DE50) {
            // Read scratch register 1.
            return packetlib_read_scratch2();
        }
    }

    // The net dimm seems a lot happier if we return nonzero values for
    // its random reads that it does.
    return 0xFFFFFFFF;
}

void poke_memory(unsigned int address, int size, uint32_t data)
{
    if (size == 4)
    {
        if ((address & 0xFFFFFF) == 0xC0DE10) {
            // Write data register.
            write_data(data);
        }
        else if ((address & 0xFFFFFF) == 0xC0DE20) {
            // Write status register.
            write_send_status(data);
        }
        else if ((address & 0xFFFFFF) == 0xC0DE30) {
            // Write status register.
            write_recv_status(data);
        }
        else if ((address & 0xFFFFFF) == 0xC0DE40) {
            // Write scratch register 1.
            packetlib_write_scratch1(data);
        }
        else if ((address & 0xFFFFFF) == 0xC0DE50) {
            // Write scratch register 2.
            packetlib_write_scratch2(data);
        }
    }
}

void packetlib_render_stats(char *buffer)
{
    // Display information about current packet library.
    packetlib_stats_t stats = packetlib_stats();
    sprintf(
        buffer,
        "Total packets sent: %d\nTotal packets received: %d\n"
        "Cancelled packets: %d\nChecksum errors: %d\n"
        "Pending packets: %d to send, %d to receive\n"
        "Send in progress: %s\nReceive in progress: %s",
        stats.packets_sent,
        stats.packets_received,
        stats.packets_cancelled,
        stats.checksum_errors,
        stats.packets_pending_send,
        stats.packets_pending_receive,
        stats.send_in_progress ? "yes" : "no",
        stats.receive_in_progress ? "yes" : "no"
    );
}
