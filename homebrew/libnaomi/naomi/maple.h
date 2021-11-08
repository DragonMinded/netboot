#ifndef __MAPLE_H
#define __MAPLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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

// The following allow you to interact with the maple device
// directly. Note that since there is only one maple device and
// it cannot respond to multiple threads that these are all protected
// by a mutex. In the instance that they cannot get hardware access
// they will return a negative value. They will return zero on success.
int maple_request_reset();
int maple_request_version(char *outptr);
int maple_request_self_test();
int maple_request_update(void *binary, unsigned int len);
int maple_request_eeprom_read(uint8_t *outbytes);
int maple_request_eeprom_write(uint8_t *inbytes);

// The following allow you to interact with JVS IOs on the JVS
// bus through the maple interface. They all return 0 on success
// or a negative value to indicate that they could not lock the hardware.
int maple_request_jvs_reset(uint8_t addr);
int maple_request_jvs_assign_address(uint8_t old_addr, uint8_t new_addr);
int maple_request_jvs_id(uint8_t addr, char *outptr);
int maple_request_jvs_buttons(uint8_t addr, jvs_buttons_t *buttons);

// The following is meant to be a slightly higher-level API for polling
// for inputs. Run maple_poll_buttons() once per frame (or per polling
// period that you desire) to load the current buttons. Then you can use
// maple_buttons_pressed(), maple_buttons_released() and maple_buttons_held()
// to read the list of buttons pressed since last poll, released since last
// poll and the current value of all buttons and analog sticks. Note that
// analog sticks are never available in maple_buttons_pressed() or
// maple_buttons_released(). Note that attempting to poll buttons and ask
// about pressed/released/current state is indeterminate across multiple threads,
// so it is recommended to keep calls to all four of these functions in a single
// thread. If maple_poll_buttons cannot get exclusive access to the hardware
// it will return nonzero to indicate that buttons were not updated. Otherwise
// it will return zero to indicate that buttons were updated successfully.
int maple_poll_buttons();
jvs_buttons_t maple_buttons_pressed();
jvs_buttons_t maple_buttons_released();
jvs_buttons_t maple_buttons_held();

#ifdef __cplusplus
}
#endif

#endif
