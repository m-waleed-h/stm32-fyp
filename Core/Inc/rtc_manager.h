#ifndef RTC_MANAGER_H
#define RTC_MANAGER_H

/**
 * rtc_manager.h
 * -------------
 * DS3231 RTC wrapper for FYP Traffic Controller.
 *
 * The DS3231 is configured to output a 1Hz square wave on its INT/SQW pin,
 * which is connected to PC3 (EXTI3) on the STM32.
 * Every rising edge triggers HAL_GPIO_EXTI_Callback → RTC_Tick() →
 * releases rtcTickSemaphore once per second.
 *
 * Traffic tasks acquire this semaphore to count real seconds —
 * more accurate than osDelay for a real-time controller.
 */

#include "main.h"
#include "cmsis_os.h"
#include "ds3231.h"

// ---------------------------------------------------------
// Semaphore: released once per second by RTC 1Hz interrupt.
// Acquire N times to wait N seconds.
// ---------------------------------------------------------
extern osSemaphoreId_t rtcTickSemaphore;

/**
 * @brief  Initialize DS3231 and create the tick semaphore.
 *         Call AFTER osKernelInitialize(), BEFORE osKernelStart().
 * @param  hi2c  Pointer to I2C handle (hi2c1)
 */
void RTC_Manager_Init(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Call this from HAL_GPIO_EXTI_Callback when RTC_INT_In_Pin fires.
 *         Releases rtcTickSemaphore — safe to call from ISR context.
 */
void RTC_Tick(void);

/**
 * @brief  Returns 1 if current time is rush hour, 0 otherwise.
 *         Rush hour: 7:00–9:00 AM and 17:00–19:00 (5–7 PM).
 */
uint8_t RTC_IsRushHour(void);

/**
 * @brief  Returns 1 if current time is night (23:00–5:00).
 *         Night mode: shorter cycle, lower activity.
 */
uint8_t RTC_IsNight(void);

/**
 * @brief  Get current hour (0–23) from DS3231.
 */
uint8_t RTC_GetHour(void);

#endif /* RTC_MANAGER_H */
