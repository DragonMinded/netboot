#ifndef __MAPLE_H
#define __MAPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define MAPLE_BASE 0xA05F6C00

#define MAPLE_DMA_BUFFER_ADDR (0x04 >> 2)
#define MAPLE_DMA_TRIGGER_SELECT (0x10 >> 2)
#define MAPLE_DEVICE_ENABLE (0x14 >> 2)
#define MAPLE_DMA_START (0x18 >> 2)
#define MAPLE_TIMEOUT_AND_SPEED (0x80 >> 2)
#define MAPLE_STATUS (0x84 >> 2)
#define MAPLE_DMA_TRIGGER_CLEAR (0x88 >> 2)
#define MAPLE_DMA_HW_INIT (0x8C >> 2)
#define MAPLE_ENDIAN_SELECT (0x0E8 >> 2)

#define MAPLE_ADDRESS_RANGE(x) ((x >> 20) - 0x80)

#define MAPLE_DEVICE_INFO_REQUEST 0x01
#define MAPLE_DEVICE_RESET_REQUEST 0x03
#define MAPLE_DEVICE_INFO_RESPONSE 0x05
#define MAPLE_COMMAND_ACKNOWLEDGE_RESPONSE 0x07
#define MAPLE_NAOMI_UPLOAD_CODE_REQUEST 0x80
#define MAPLE_NAOMI_UPLOAD_CODE_RESPONSE 0x81
#define MAPLE_NAOMI_VERSION_REQUEST 0x82
#define MAPLE_NAOMI_VERSION_RESPONSE 0x83
#define MAPLE_NAOMI_SELF_TEST_REQUEST 0x84
#define MAPLE_NAOMI_SELF_TEST_RESPONSE 0x85
#define MAPLE_NAOMI_IO_REQUEST 0x86
#define MAPLE_NAOMI_IO_RESPONSE 0x87

#define MAPLE_NO_RESPONSE 0xFF
#define MAPLE_BAD_FUNCTION_CODE 0xFE
#define MAPLE_UNKNOWN_COMMAND 0xFD
// Under most circumstances, an 0xFC response includes 0 words of
// data, giving no reason. However, the MIE will sometimes send a
// 1-word response. In this case, the word represents the error that
// caused an 0xFC to be generated. Those are as follows:
//
// 0x1 - Parity error on command receipt.
// 0x2 - Overflow error on command receipt.
#define MAPLE_RESEND_COMMAND 0xFC

// Values that get returned in various JVS packets to inform us
// whether we have a working JVS IO and whether it is addressed.
#define JVS_SENSE_DISCONNECTED 0x1
#define JVS_SENSE_ADDRESSED 0x2

typedef struct jvs_status
{
    uint8_t jvs_present_bitmask;
    uint8_t psw1;
    uint8_t psw2;
    uint8_t dip_switches;
    unsigned int packet_length;
    uint8_t packet[128];
} jvs_status_t;

typedef struct player_buttons
{
    uint8_t service;
    uint8_t start;
    uint8_t up;
    uint8_t down;
    uint8_t left;
    uint8_t right;
    uint8_t button1;
    uint8_t button2;
    uint8_t button3;
    uint8_t button4;
    uint8_t button5;
    uint8_t button6;
    uint8_t analog1;
    uint8_t analog2;
    uint8_t analog3;
    uint8_t analog4;
} player_buttons_t;

typedef struct jvs_buttons
{
    uint8_t dip1;
    uint8_t dip2;
    uint8_t dip3;
    uint8_t dip4;
    uint8_t psw1;
    uint8_t psw2;
    uint8_t test;
    player_buttons_t player1;
    player_buttons_t player2;
} jvs_buttons_t;

// It is not necessary to call these two functions, as they are handled
// for you automatically by the runtime.
void maple_init();
void maple_free();

void maple_wait_for_dma();
uint32_t *maple_swap_data(unsigned int port, int peripheral, unsigned int cmd, unsigned int datalen, void *data);
int maple_response_valid(uint32_t *response);
uint8_t maple_response_code(uint32_t *response);
uint8_t maple_response_payload_length_words(uint32_t *response);
uint32_t *maple_skip_response(uint32_t *response);

int maple_busy();
void maple_wait_for_ready();
void maple_request_reset();
void maple_request_version(char *outptr);
int maple_request_self_test();
int maple_request_update(void *binary, unsigned int len);
int maple_request_eeprom_read(uint8_t *outbytes);
int maple_request_eeprom_write(uint8_t *inbytes);

int maple_request_send_jvs(uint8_t addr, unsigned int len, void *bytes);
jvs_status_t maple_request_recv_jvs();
int jvs_packet_valid(uint8_t *data);
unsigned int jvs_packet_payload_length_bytes(uint8_t *data);
unsigned int jvs_packet_code(uint8_t *data);
uint8_t *jvs_packet_payload(uint8_t *data);
void maple_request_jvs_reset(uint8_t addr);
void maple_request_jvs_assign_address(uint8_t old_addr, uint8_t new_addr);
int maple_request_jvs_id(uint8_t addr, char *outptr);
jvs_buttons_t maple_request_jvs_buttons(uint8_t addr);

#ifdef __cplusplus
}
#endif

#endif
