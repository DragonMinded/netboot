#include <stdint.h>
#include <naomi/eeprom.h>
#include <naomi/video.h>
#include "common.h"
#include "config.h"

#define CONFIG_MEMORY_LOCATION 0x0D000000
#define GAMES_POINTER_LOC 0
#define GAMES_COUNT_LOC 4
#define ENABLE_ANALOG_LOC 8
#define ENABLE_DEBUG_LOC 12
#define DEFAULT_SELECTION_LOC 16
#define SYSTEM_REGION_LOC 20
#define USE_FILENAMES_LOC 24

config_t *get_config()
{
    return (config_t *)CONFIG_MEMORY_LOCATION;
}

games_list_t *get_games_list(unsigned int *count)
{
    // Index into config memory to grab the count of games, as well as the offset pointer
    // to where the games blob is.
    config_t *config = get_config();
    *count = config->games_count;
    return (games_list_t *)(CONFIG_MEMORY_LOCATION + config->game_list_offset);
}

uint8_t *get_fallback_font(unsigned int *size)
{
    config_t *config = get_config();
    *size = config->fallback_font_size;
    if (config->fallback_font_size && config->fallback_font_offset)
    {
        return (uint8_t *)(CONFIG_MEMORY_LOCATION + config->fallback_font_offset);
    }
    else
    {
        return 0;
    }
}
