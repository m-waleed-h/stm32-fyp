/**
 * traffic_controller.c
 * --------------------
 * 2-panel traffic light sequencing using FreeRTOS + DS3231 RTC ticks.
 *
 * Timing logic:
 *   - Rush hour  (7-9 AM, 5-7 PM) → greenSeconds = 8
 *   - Night mode (11 PM - 5 AM)   → greenSeconds = 3
 *   - Normal                       → greenSeconds = 5
 *   - Manual override via Traffic_SetGreenTime() (e.g. from UART task)
 */

#include "traffic_controller.h"
#include "rtc_manager.h"

// ============================================================
// Light pattern bit definitions
// ============================================================
// Bit:   5        4        3        2        1        0
// Pin: LATCH6  LATCH5  LATCH4  LATCH3  LATCH2  LATCH1
//      P2_GRN  P2_YLW  P2_RED  P1_GRN  P1_YLW  P1_RED

#define P1_RED    (1 << 0)   // LATCH_1 — PB4 — Panel 1 Red
#define P1_YELLOW (1 << 1)   // LATCH_2 — PB5 — Panel 1 Yellow
#define P1_GREEN  (1 << 2)   // LATCH_3 — PB0 — Panel 1 Green
#define P2_RED    (1 << 3)   // LATCH_4 — PB1 — Panel 2 Red
#define P2_YELLOW (1 << 4)   // LATCH_5 — PA1 — Panel 2 Yellow
#define P2_GREEN  (1 << 5)   // LATCH_6 — PA0 — Panel 2 Green

// ============================================================
// Private state
// ============================================================

typedef enum {
    TIMING_AUTO,    // RTC-based automatic (rush hour / night / normal)
    TIMING_MANUAL   // Overridden via Traffic_SetGreenTime() or UART
} TimingMode_t;

static volatile uint32_t   greenSeconds  = 5;
static volatile uint32_t   yellowSeconds = 2;  // always fixed
static volatile TimingMode_t timingMode  = TIMING_AUTO;

// ============================================================
// Private: Set light pattern on latch
// ============================================================

static void SetLights(uint8_t pattern)
{
    // Write 6 data bits to LATCH_1 through LATCH_6
    HAL_GPIO_WritePin(LATCH_1_GPIO_Port, LATCH_1_Pin,
                      ((pattern >> 0) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_2_GPIO_Port, LATCH_2_Pin,
                      ((pattern >> 1) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_3_GPIO_Port, LATCH_3_Pin,
                      ((pattern >> 2) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_4_GPIO_Port, LATCH_4_Pin,
                      ((pattern >> 3) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_5_GPIO_Port, LATCH_5_Pin,
                      ((pattern >> 4) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_6_GPIO_Port, LATCH_6_Pin,
                      ((pattern >> 5) & 1) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // Enable CBB1 (OE# = LOW = active), disable CBB2 and CBB3
    HAL_GPIO_WritePin(LATCH_SELECT_1_GPIO_Port, LATCH_SELECT_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_SELECT_2_GPIO_Port, LATCH_SELECT_2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LATCH_SELECT_3_GPIO_Port, LATCH_SELECT_3_Pin, GPIO_PIN_SET);
}

// ============================================================
// Private: Wait N real seconds using RTC 1Hz semaphore
// ============================================================

static void WaitSeconds(uint32_t seconds)
{
    for (uint32_t i = 0; i < seconds; i++) {
        // Block until ISR releases semaphore (fires every 1 second)
        osSemaphoreAcquire(rtcTickSemaphore, osWaitForever);
    }
}

// ============================================================
// Private: Update green time from RTC (auto mode only)
// ============================================================

static void UpdateTimingFromRTC(void)
{
    if (timingMode != TIMING_AUTO) return;

    if (RTC_IsRushHour()) {
        greenSeconds = 8;   // Peak traffic — longer green
    } else if (RTC_IsNight()) {
        greenSeconds = 3;   // Low traffic — shorter cycle
    } else {
        greenSeconds = 5;   // Normal
    }
}

// ============================================================
// Public API
// ============================================================

void Traffic_Init(void)
{
    // All lights off at startup
    SetLights(0x00);
}

void Traffic_SetGreenTime(uint32_t seconds)
{
    // Clamp to safe range
    if (seconds < 3)  seconds = 3;
    if (seconds > 30) seconds = 30;

    greenSeconds = seconds;
    timingMode   = TIMING_MANUAL;  // Disable auto-RTC override
}

// ============================================================
// FreeRTOS Task
// ============================================================

void TrafficManagerTask(void *argument)
{
    Traffic_Init();

    for (;;)
    {
        // Re-evaluate timing at start of each full cycle
        UpdateTimingFromRTC();

        // --------------------------------------------------
        // Phase 1: Panel 1 GREEN + Panel 2 RED
        // --------------------------------------------------
        SetLights(P1_GREEN | P2_RED);
        HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_SET); // debug LED on
        WaitSeconds(greenSeconds);

        // --------------------------------------------------
        // Phase 2: Panel 1 YELLOW + Panel 2 RED
        // --------------------------------------------------
        SetLights(P1_YELLOW | P2_RED);
        WaitSeconds(yellowSeconds);

        // --------------------------------------------------
        // Phase 3: Panel 1 RED + Panel 2 GREEN
        // --------------------------------------------------
        SetLights(P1_RED | P2_GREEN);
        HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_RESET); // debug LED off
        WaitSeconds(greenSeconds);

        // --------------------------------------------------
        // Phase 4: Panel 1 RED + Panel 2 YELLOW
        // --------------------------------------------------
        SetLights(P1_RED | P2_YELLOW);
        WaitSeconds(yellowSeconds);

        // Loop → back to Phase 1
    }
}
