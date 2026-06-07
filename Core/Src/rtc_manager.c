/**
 * rtc_manager.c
 * -------------
 * DS3231 RTC wrapper — 1Hz tick semaphore + time-based helpers.
 */

#include "rtc_manager.h"
#include "ds3231.h"

// Semaphore released by ISR every 1 second
osSemaphoreId_t rtcTickSemaphore;

// Private I2C handle pointer
static I2C_HandleTypeDef *_hi2c;

// ============================================================
// Public: Init
// ============================================================

void RTC_Manager_Init(I2C_HandleTypeDef *hi2c)
{
    _hi2c = hi2c;

    // DS3231 already initialized in main.c — just store handle.
    // Square wave @ 1Hz already configured (S3231_SQUARE_WAVE_INTERRUPT).
    DS3231_Init(hi2c);
    DS3231_SetInterruptMode(DS3231_SQUARE_WAVE_INTERRUPT);
    DS3231_SetRateSelect(DS3231_1HZ);
    // Create semaphore with initial count = 0.
    // ISR will release it; task will acquire it.
    rtcTickSemaphore = osSemaphoreNew(
        1,    // max count (binary semaphore)
        0,    // initial count (starts locked)
        NULL
    );
}

// ============================================================
// Public: ISR-safe tick — called from HAL_GPIO_EXTI_Callback
// ============================================================

void RTC_Tick(void)
{
    // osSemaphoreRelease is ISR-safe in CMSIS-RTOS v2
    osSemaphoreRelease(rtcTickSemaphore);
}

// ============================================================
// Private: read current hour from DS3231
// ============================================================

static uint8_t GetHourFromRTC(void)
{
    RTC_TimeDate t;
    RTC_GetCurrentTime(&t);  // adjust if your ds3231 lib uses different function name
    return t.hours;
}

// ============================================================
// Public: time-of-day helpers
// ============================================================

uint8_t RTC_GetHour(void)
{
    return GetHourFromRTC();
}

uint8_t RTC_IsRushHour(void)
{
    uint8_t h = GetHourFromRTC();
    // Morning rush: 7 AM – 9 AM
    // Evening rush: 5 PM – 7 PM
    return ((h >= 7 && h < 9) || (h >= 17 && h < 19));
}

uint8_t RTC_IsNight(void)
{
    uint8_t h = GetHourFromRTC();
    // Night: 11 PM – 5 AM
    return (h >= 23 || h < 5);
}
