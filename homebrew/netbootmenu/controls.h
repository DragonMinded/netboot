#ifndef __CONTROLS_H
#define __CONTROLS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
    // The following controls only ever need a pressed event.
    uint8_t up_pressed;
    uint8_t down_pressed;
    uint8_t left_pressed;
    uint8_t right_pressed;
    uint8_t test_pressed;
    uint8_t service_pressed;

    // The following controls need pressed and released events to detect holds.
    uint8_t start_pressed;
    uint8_t start_released;
} controls_t;

controls_t get_controls(state_t *state, int reinit);

#ifdef __cplusplus
}
#endif

#endif
