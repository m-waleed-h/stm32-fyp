#ifndef TRAFFIC_CONTROLLER_H
#define TRAFFIC_CONTROLLER_H

/**
 * traffic_controller.h
 * --------------------
 * FreeRTOS-based 2-panel traffic light controller.
 *
 * Hardware:
 *   Panel 1 (Direction A): LATCH_1=Red, LATCH_2=Yellow, LATCH_3=Green
 *   Panel 2 (Direction B): LATCH_4=Red, LATCH_5=Yellow, LATCH_6=Green
 *   All driven via TC74HC573APF latch, enabled by LATCH_SELECT_1 (PC0).
 *
 * Timing:
 *   Uses DS3231 1Hz semaphore (rtcTickSemaphore) for real-time accuracy.
 *   Green duration adjusts automatically based on time of day via RTC.
 */

#include "main.h"
#include "cmsis_os.h"

/**
 * @brief  Initialize traffic controller.
 *         Call AFTER RTC_Manager_Init(), BEFORE osKernelStart().
 */
// Current light state — shared with uart_handler for status TX
typedef struct {
    uint8_t  activePanel;      // 1 or 2
    char     state[8];         // "green", "yellow", "night"
    uint32_t greenSeconds;     // current green duration
    uint8_t  healthOk;         // 1=ok, 0=fault
    uint8_t  nightMode;        // 1=night blink active
} TrafficState_t;

extern volatile TrafficState_t gTrafficState;


void Traffic_Init(void);

/**
 * @brief  Manually override green time (seconds).
 *         Will be used when Orange Pi sends UART density data.
 * @param  seconds  New green phase duration (3–30 recommended)
 */
void Traffic_SetGreenTime(uint32_t seconds);

/**
 * @brief  FreeRTOS task — traffic light sequencing.
 *         Do NOT call directly. Pass to osThreadNew().
 */
void TrafficManagerTask(void *argument);

#endif /* TRAFFIC_CONTROLLER_H */
