#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "naomi/video.h"
#include "naomi/maple.h"
#include "naomi/eeprom.h"
#include "naomi/system.h"
#include "naomi/timer.h"
#include "naomi/dimmcomms.h"

#define MAX_OUTSTANDING_PACKETS 268
#define MAX_PACKET_LENGTH 253

typedef struct
{
    uint8_t data[MAX_PACKET_LENGTH];
    unsigned int len;
} packet_t;

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
} packetlib_state_t;

packetlib_state_t packetlib_state;

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
    return (((~sum) & 0xFF) << 16) | value & 0x0000FFFF;
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

#define MAX_MESSAGE_LENGTH 0xFFFF
#define MESSAGE_HEADER_LENGTH 8
#define MAX_MESSAGE_DATA_LENGTH (MAX_PACKET_LENGTH - MESSAGE_HEADER_LENGTH)
#define MESSAGE_ID_LOC 0
#define MESSAGE_SEQ_LOC 2
#define MESSAGE_LEN_LOC 4
#define MESSAGE_LOC_LOC 6
#define MESSAGE_DATA_LOC 8

int message_send(uint16_t type, void * data, unsigned int length)
{
    uint8_t buffer[MAX_PACKET_LENGTH];
    static uint16_t sequence = 1;

    if (length > MAX_MESSAGE_LENGTH)
    {
        return -3;
    }

    // We always want to run this loop at least one time, so we can send
    // packets of 0 bytes in length.
    for (unsigned int loc = 0; (loc == 0 || loc < length); loc += MAX_MESSAGE_DATA_LENGTH)
    {
        unsigned int packet_len = length - loc;
        if (packet_len > MAX_MESSAGE_DATA_LENGTH)
        {
            packet_len = MAX_MESSAGE_DATA_LENGTH;
        }

        // Set up packet type in header.
        uint16_t tmp = type;
        memcpy(&buffer[MESSAGE_ID_LOC], &tmp, 2);

        // Set up sequence number in header.
        memcpy(&buffer[MESSAGE_SEQ_LOC], &sequence, 2);

        // Set up packet length in header.
        tmp = length;
        memcpy(&buffer[MESSAGE_LEN_LOC], &tmp, 2);

        // Set up current packet location in header.
        tmp = loc;
        memcpy(&buffer[MESSAGE_LOC_LOC], &tmp, 2);

        if (packet_len > 0)
        {
            // Finally, copy the data in.
            memcpy(&buffer[MESSAGE_DATA_LOC], ((uint8_t *)data) + loc, packet_len);
        }

        // Now, send the packet.
        if (packetlib_send(buffer, packet_len + MESSAGE_HEADER_LENGTH) != 0)
        {
            return -4;
        }
    }

    // We finished this packet, set the sequence number to something else for
    // the next packet.
    sequence ++;
    if (sequence == 0)
    {
        // Don't want sequence ID 0 for reassembly purposes.
        sequence = 1;
    }
}

int message_recv(uint16_t *type, void ** data, unsigned int *length)
{
    // Figure out if there is a packet worth assembling. This is a really gross,
    // inefficient algorithm, but whatever its good enough for now.
    uint8_t *reassembled_data = 0;
    int success = -5;
    uint16_t seen_packet_sequences[MAX_OUTSTANDING_PACKETS];
    uint8_t *seen_positions[MAX_OUTSTANDING_PACKETS];
    uint16_t seen_packet_lengths[MAX_OUTSTANDING_PACKETS];
    memset(seen_packet_sequences, 0, sizeof(uint16_t) * MAX_OUTSTANDING_PACKETS);
    memset(seen_positions, 0, sizeof(uint8_t *) * MAX_OUTSTANDING_PACKETS);
    memset(seen_packet_lengths, 0, sizeof(uint16_t) * MAX_OUTSTANDING_PACKETS);

    for (unsigned int pkt = 0; pkt < MAX_OUTSTANDING_PACKETS; pkt++)
    {
        // Grab the potential packet we could receive.
        unsigned int pkt_length = 0;
        uint8_t *pkt_data = packetlib_peek(pkt, &pkt_length);
        if (pkt_data == 0)
        {
            // No data for this packet.
            continue;
        }
        if (pkt_length < MESSAGE_HEADER_LENGTH)
        {
            // Toss bogus packet.
            packetlib_discard(pkt);
            continue;
        }

        // Grab the sequence number from this packet.
        uint16_t sequence;
        unsigned int index;
        memcpy(&sequence, &pkt_data[MESSAGE_SEQ_LOC], 2);

        if (sequence == 0)
        {
            // Toss bogus packet.
            packetlib_discard(pkt);
            continue;
        }

        // Grab the length and needed total packets for this packet.
        uint16_t msg_length;
        memcpy(&msg_length, &pkt_data[MESSAGE_LEN_LOC], 2);
        unsigned int num_packets_needed = (msg_length + (MAX_MESSAGE_DATA_LENGTH - 1)) / MAX_MESSAGE_DATA_LENGTH;

        // Find the positions data for this sequence.
        for (unsigned int i = 0; i < MAX_OUTSTANDING_PACKETS; i++)
        {
            if (seen_packet_sequences[i] == sequence)
            {
                index = i;
                break;
            }
            if (seen_packet_sequences[i] == 0)
            {
                // The index doesn't exist, lets create it.
                index = i;

                // Calculate how many parts of the message we need to see.
                seen_packet_sequences[index] = sequence;
                seen_packet_lengths[index] = msg_length;
                if (num_packets_needed > 0)
                {
                    seen_positions[index] = malloc(num_packets_needed);
                    memset(seen_positions[index], 0, num_packets_needed);
                }
                break;
            }
        }

        if (num_packets_needed > 0)
        {
            // Now, mark the particular portion of this packet as present.
            uint16_t location;
            memcpy(&location, &pkt_data[MESSAGE_LOC_LOC], 2);
            seen_positions[index][location / MAX_MESSAGE_DATA_LENGTH] = 1;
        }
    }

    // Now that we've gathered up which packets we have, see if any packets
    // we care about are fully received.
    for (unsigned int index = 0; index < MAX_OUTSTANDING_PACKETS; index++)
    {
        if (seen_packet_sequences[index] == 0)
        {
            // We ran out of packet sequences we're tracking.
            break;
        }

        unsigned int num_packets_needed = (seen_packet_lengths[index] + (MAX_MESSAGE_DATA_LENGTH - 1)) / MAX_MESSAGE_DATA_LENGTH;
        int ready = 1;

        for (unsigned int i = 0; i < num_packets_needed; i++)
        {
            if (!seen_positions[index][i])
            {
                // This packet is not ready.
                ready = 0;
                break;
            }
        }

        if (ready)
        {
            // This packet is ready!
            if (seen_packet_lengths[index] > 0)
            {
                reassembled_data = malloc(seen_packet_lengths[index]);
            }
            *data = reassembled_data;
            *length = seen_packet_lengths[index];

            for (unsigned int pkt = 0; pkt < MAX_OUTSTANDING_PACKETS; pkt++)
            {
                // Grab the potential packet we could receive.
                unsigned int pkt_length = 0;
                uint8_t *pkt_data = packetlib_peek(pkt, &pkt_length);
                if (pkt_data == 0 || pkt_length < MESSAGE_HEADER_LENGTH)
                {
                    // No data for this packet.
                    continue;
                }

                // Grab the sequence number from this packet.
                uint16_t sequence;
                memcpy(&sequence, &pkt_data[MESSAGE_SEQ_LOC], 2);

                if (sequence != seen_packet_sequences[index])
                {
                    // This packet is not one of the ones we're after.
                    continue;
                }

                // Grab the type from this packet. This is inefficient since we
                // only need to do it once, but whatever. Its two whole bytes and
                // this entire reassembly algorithm could use work.
                memcpy(type, &pkt_data[MESSAGE_ID_LOC], 2);

                if (seen_packet_lengths[index] > 0)
                {
                    // Grab the location from this packet, so we can copy it into
                    // the right spot in the destination.
                    uint16_t location;
                    memcpy(&location, &pkt_data[MESSAGE_LOC_LOC], 2);

                    // Actually copy it.
                    memcpy(reassembled_data + location, &pkt_data[MESSAGE_DATA_LOC], pkt_length - MESSAGE_HEADER_LENGTH);
                }

                // We don't need this packet anymore, since we received it.
                packetlib_discard(pkt);
            }

            // We finished assembling the packet, lets return it!
            success = 0;
            break;
        }
    }

    // Need to free a bunch of stuff.
    for (unsigned int index = 0; index < MAX_OUTSTANDING_PACKETS; index++)
    {
        if (seen_positions[index])
        {
            free(seen_positions[index]);
        }
    }

    // Return the possibly reassembled packet.
    return success;
}

#define CONFIG_MEMORY_LOCATION 0x0D000000
#define GAMES_POINTER_LOC 0
#define GAMES_COUNT_LOC 4
#define ENABLE_ANALOG_LOC 8
#define ENABLE_DEBUG_LOC 12

typedef struct __attribute__((__packed__))
{
    char name[128];
    uint8_t serial[4];
    unsigned int id;
} games_list_t;

games_list_t *get_games_list(unsigned int *count)
{
    uint32_t config = CONFIG_MEMORY_LOCATION;

    // Index into config memory to grab the count of games, as well as the offset pointer
    // to where the games blob is.
    *count = *((uint32_t *)(config + GAMES_COUNT_LOC));
    return (games_list_t *)(*((uint32_t *)(config + GAMES_POINTER_LOC)) + CONFIG_MEMORY_LOCATION);
}

unsigned int analog_enabled()
{
    uint32_t config = CONFIG_MEMORY_LOCATION;
    return *((uint32_t *)(config + ENABLE_ANALOG_LOC));
}

unsigned int debug_enabled()
{
    uint32_t config = CONFIG_MEMORY_LOCATION;
    return *((uint32_t *)(config + ENABLE_DEBUG_LOC));
}

unsigned int repeat(unsigned int cur_state, int *repeat_count)
{
    // Based on 60fps. A held button will "repeat" itself ~16x a second
    // after a 0.5 second hold delay.
    if (*repeat_count < 0)
    {
        // If we have never pushed this button, don't try repeating
        // if it happened to be held.
        return 0;
    }

    if (cur_state == 0)
    {
        // Button isn't held, no repeats.
        *repeat_count = 0;
        return 0;
    }

    int count = *repeat_count;
    *repeat_count = count + 1;

    if (count >= 30)
    {
        // Repeat every 1/16 second after 0.5 second.
        return (count % 4) ? 0 : 1;
    }
}

void repeat_init(unsigned int pushed_state, int *repeat_count)
{
    if (pushed_state == 0)
    {
        // Haven't pushed the button yet.
        return;
    }
    if (*repeat_count < 0)
    {
        // Mark that we've seen this button pressed.
        *repeat_count = 0;
    }
}

#define MESSAGE_SELECTION 0x1000

#define ANALOG_CENTER 0x80
#define ANALOG_THRESH_ON 0x30
#define ANALOG_THRESH_OFF 0x20

extern uint8_t *helvetica_ttf_data;
extern unsigned int helvetica_ttf_len;

void main()
{
    // Grab the system configuration
    eeprom_t settings;
    eeprom_read(&settings);

    // Attach our communication handler for packet sending/receiving.
    packetlib_init();

    // Init the screen for a simple 640x480 framebuffer.
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));

    // Attach our font
    font_t *helvetica = video_font_add(helvetica_ttf_data, helvetica_ttf_len);
    video_font_set_size(helvetica, 14);

    // Grab our configuration.
    unsigned int cursor = 0;
    unsigned int top = 0;
    int selection = -1;
    unsigned int count = 0;
    unsigned int oldaup[2] = { 0 };
    unsigned int oldadown[2] = { 0 };
    unsigned int aup[2] = { 0 };
    unsigned int adown[2] = { 0 };
    int repeats[4] = { -1, -1, -1, -1 };
    games_list_t *games = get_games_list(&count);

    // Leave 24 pixels of padding on top and bottom of the games list.
    // Space out games 16 pixels across.
    unsigned int maxgames = (video_height() - (24 + 16)) / 16;

    // FPS calculation for debugging.
    double fps_value = 0.0;

    while ( 1 )
    {
        // Get FPS measurements.
        int fps = profile_start();

        // First, poll the buttons and act accordingly.
        maple_poll_buttons();
        jvs_buttons_t pressed = maple_buttons_pressed();
        jvs_buttons_t held = maple_buttons_current();

        // Also calculate analog thresholds so we can emulate joystick with analog.
        if (analog_enabled())
        {
            if (held.player1.analog1 < (ANALOG_CENTER - ANALOG_THRESH_ON))
            {
                aup[0] = 1;
            }
            else if (held.player1.analog1 > (ANALOG_CENTER - ANALOG_THRESH_OFF))
            {
                aup[0] = 0;
            }

            if (held.player2.analog1 < (ANALOG_CENTER - ANALOG_THRESH_ON))
            {
                aup[1] = 1;
            }
            else if (held.player2.analog1 > (ANALOG_CENTER - ANALOG_THRESH_OFF))
            {
                aup[1] = 0;
            }

            if (held.player1.analog1 > (ANALOG_CENTER + ANALOG_THRESH_ON))
            {
                adown[0] = 1;
            }
            else if (held.player1.analog1 < (ANALOG_CENTER + ANALOG_THRESH_OFF))
            {
                adown[0] = 0;
            }

            if (held.player2.analog1 > (ANALOG_CENTER + ANALOG_THRESH_ON))
            {
                adown[1] = 1;
            }
            else if (held.player2.analog1 < (ANALOG_CENTER + ANALOG_THRESH_OFF))
            {
                adown[1] = 0;
            }

            // Map analogs back onto digitals.
            if (aup[0])
            {
                held.player1.up = 1;
            }
            if (aup[1])
            {
                held.player2.up = 1;
            }
            if (adown[0])
            {
                held.player1.down = 1;
            }
            if (adown[1])
            {
                held.player2.down = 1;
            }
            if (aup[0] && !oldaup[0])
            {
                pressed.player1.up = 1;
            }
            if (aup[1] && !oldaup[1])
            {
                pressed.player2.up = 1;
            }
            if (adown[0] && !oldadown[0])
            {
                pressed.player1.down = 1;
            }
            if (adown[1] && !oldadown[1])
            {
                pressed.player2.down = 1;
            }

            memcpy(oldaup, aup, sizeof(aup));
            memcpy(oldadown, adown, sizeof(adown));
        }

        // Process buttons and repeats.
        unsigned int up = 0;
        unsigned int down = 0;

        if (pressed.test || pressed.psw1)
        {
            // Request to go into system test mode.
            enter_test_mode();
        }
        else if (pressed.player1.start || (settings.system.players >= 2 && pressed.player2.start))
        {
            // Made a selection!
            message_send(MESSAGE_SELECTION, &cursor, 4);
        }
        else
        {
            if (pressed.player1.up || (settings.system.players >= 2 && pressed.player2.up))
            {
                up = 1;

                repeat_init(pressed.player1.up, &repeats[0]);
                repeat_init(pressed.player2.up, &repeats[1]);
            }
            else if (pressed.player1.down || (settings.system.players >= 2 && pressed.player2.down))
            {
                down = 1;

                repeat_init(pressed.player1.down, &repeats[2]);
                repeat_init(pressed.player2.down, &repeats[3]);
            }
            if (repeat(held.player1.up, &repeats[0]) || (settings.system.players >= 2 && repeat(held.player2.up, &repeats[1])))
            {
                up = 1;
            }
            else if (repeat(held.player1.down, &repeats[2]) || (settings.system.players >= 2 && repeat(held.player2.down, &repeats[3])))
            {
                down = 1;
            }
        }

        // Act on cursor.
        if (up)
        {
            // Moved cursor up.
            if (cursor > 0)
            {
                cursor --;
            }
            if (cursor < top)
            {
                top = cursor;
            }
        }
        else if (down)
        {
            // Moved cursor down.
            if (cursor < (count - 1))
            {
                cursor ++;
            }
            if (cursor >= (top + maxgames))
            {
                top = cursor - (maxgames - 1);
            }
        }

        // Now, see if we have a packet to handle.
        {
            uint16_t type = 0;
            uint8_t *data = 0;
            unsigned int length = 0;
            if (message_recv(&type, (void *)&data, &length) == 0)
            {
                // Respond to packets here.

                if (data != 0)
                {
                    free(data);
                }
            }
        }

        // Now, render the actual list of games.
        {
            for (unsigned int game = top; game < top + maxgames; game++)
            {
                if (game >= count)
                {
                    // Ran out of games to display.
                    break;
                }

                // Draw cursor itself.
                if (game == cursor) {
                    video_draw_debug_text(16, 24 + ((game - top) * 16), rgb(255, 255, 20), ">");
                }

                // Draw game, highlighted if it is selected.
                video_draw_text(32, 20 + ((game - top) * 16), helvetica, game == cursor ? rgb(255, 255, 20) : rgb(255, 255, 255), games[game].name);
            }
        }

        if (debug_enabled())
        {
            // Display some debugging info.
            video_draw_debug_text((video_width() / 2) - (18 * 4), video_height() - 16, rgb(0, 200, 255), "FPS: %.01f, %dx%d", fps_value, video_width(), video_height());
        }

        // Actually draw the buffer.
        video_wait_for_vblank();
        video_display();

        uint32_t uspf = profile_end(fps);
        fps_value = (1000000.0 / (double)uspf) + 0.01;
    }
}

void test()
{
    // Initialize a simple console
    video_init_simple();
    video_set_background_color(rgb(0, 0, 0));

    while ( 1 )
    {
        // First, poll the buttons and act accordingly.
        maple_poll_buttons();
        jvs_buttons_t buttons = maple_buttons_pressed();

        if (buttons.psw1 || buttons.test)
        {
            // Request to go into system test mode.
            enter_test_mode();
        }

        video_draw_debug_text(320 - 56, 236, rgb(255, 255, 255), "test mode stub\n\n[test] - exit");
        video_wait_for_vblank();
        video_display();
    }
}
