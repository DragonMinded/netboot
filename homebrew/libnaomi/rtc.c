#include <stdint.h>
#include "naomi/rtc.h"

#define AICA_RTC_SECS_H (*((volatile uint32_t *)0xa0710000))
#define AICA_RTC_SECS_L (*((volatile uint32_t *)0xa0710004))
#define AICA_RTC_WREN (*((volatile uint32_t *)0xa0710008))

uint32_t rtc_get()
{
    uint32_t val1;
    uint32_t val2;

    do
    {
        val1 = ((AICA_RTC_SECS_H & 0xffff) << 16) | (AICA_RTC_SECS_L & 0xffff);
        val2 = ((AICA_RTC_SECS_H & 0xffff) << 16) | (AICA_RTC_SECS_L & 0xffff);
    } while (val1 != val2);

    return val1;
}

void rtc_set(uint32_t newtime)
{
    uint32_t val1;
    uint32_t val2;

    do
    {
        AICA_RTC_WREN = 1;
        AICA_RTC_SECS_H = (newtime >> 16) & 0xffff;
        AICA_RTC_WREN = 1;
        AICA_RTC_SECS_L = newtime & 0xffff;
        AICA_RTC_WREN = 0;

        val1 = ((AICA_RTC_SECS_H & 0xffff) << 16) | (AICA_RTC_SECS_L & 0xffff);
        val2 = ((AICA_RTC_SECS_H & 0xffff) << 16) | (AICA_RTC_SECS_L & 0xffff);
    } while (val1 != val2);

}
