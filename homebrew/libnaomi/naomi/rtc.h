#ifndef __RTC_H
#define __RTC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// Get the current RTC time in seconds, starting from 1/1/1950. Note that
// you can use standard time functions such as time() and localtime()
// to fetch the time and date.
uint32_t rtc_get();

// Set the current RTC time in seconds, starting from 1/1/1950.
void rtc_set(uint32_t newtime);

#ifdef __cplusplus
}
#endif

#endif
