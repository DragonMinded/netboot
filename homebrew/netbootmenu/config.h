#ifndef __CONFIG_H
#define __CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "naomi/eeprom.h"
#include "naomi/video.h"

typedef struct __attribute__((__packed__))
{
    char name[128];
    uint8_t serial[4];
    unsigned int id;
} games_list_t;

typedef struct __attribute__((__packed__))
{
    uint32_t game_list_offset;
    uint32_t games_count;
    uint32_t enable_analog;
    uint32_t enable_debug;
    uint32_t boot_selection;
    uint32_t system_region;
    uint32_t use_filenames;
    uint8_t joy1_hcenter;
    uint8_t joy1_vcenter;
    uint8_t joy2_hcenter;
    uint8_t joy2_vcenter;
    uint8_t joy1_hmin;
    uint8_t joy1_hmax;
    uint8_t joy1_vmin;
    uint8_t joy1_vmax;
    uint8_t joy2_hmin;
    uint8_t joy2_hmax;
    uint8_t joy2_vmin;
    uint8_t joy2_vmax;
    uint32_t fallback_font_offset;
    uint32_t fallback_font_size;
    uint32_t force_players;
} config_t;

typedef struct
{
    eeprom_t *settings;
    config_t *config;
    double fps;
    double animation_counter;
    double test_error_counter;
    font_t *font_18pt;
    font_t *font_12pt;
} state_t;

config_t *get_config();
games_list_t *get_games_list(unsigned int *count);
uint8_t *get_fallback_font(unsigned int *size);

#ifdef __cplusplus
}
#endif

#endif
