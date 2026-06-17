/**
 * traffic_controller.c — Final Version with Logging + Fallback
 * -------------------------------------------------------------
 * Timing priority:
 *   1. Orange Pi YOLO density  → proportional green time
 *   2. Dummy fixed counts      → same algorithm (Orange Pi offline)
 *   3. RTC time-of-day         → rush hour / normal (grace period)
 *   4. Night mode              → yellow blink (23:00–05:00)
 */

#include "traffic_controller.h"
#include "rtc_manager.h"
#include "uart_handler.h"
#include "logger.h"
#include <string.h>

// ── Pin bit definitions ───────────────────────────────────────
#define P1_RED    (1 << 0)   // LATCH_1 PB4
#define P1_YELLOW (1 << 1)   // LATCH_2 PB5
#define P1_GREEN  (1 << 2)   // LATCH_3 PB0
#define P2_RED    (1 << 3)   // LATCH_4 PB1
#define P2_YELLOW (1 << 4)   // LATCH_5 PA1
#define P2_GREEN  (1 << 5)   // LATCH_6 PA0

// ── Timing constants ──────────────────────────────────────────
#define TOTAL_GREEN_BUDGET   20u
#define MIN_GREEN             4u
#define MAX_GREEN            14u
#define YELLOW_SEC            2u

// ── Fallback dummy counts (used when Orange Pi is offline) ────
// Change these to test different traffic scenarios
#define DUMMY_PANEL1_COUNT    5u
#define DUMMY_PANEL2_COUNT    5u

// After this many missed cycles → switch to dummy data
#define NO_DATA_THRESHOLD     3u

#define CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))
#define JUNCTION_NUM  1

// ── Shared state ──────────────────────────────────────────────
volatile TrafficState_t gTrafficState = {
    .activePanel  = 1,
    .state        = "red",
    .greenSeconds = 0,
    .healthOk     = 1,
    .nightMode    = 0
};

// ── Private: write light pattern to latch ────────────────────
static void SetLights(uint8_t pattern)
{
    HAL_GPIO_WritePin(LATCH_1_GPIO_Port, LATCH_1_Pin,
        ((pattern>>0)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_2_GPIO_Port, LATCH_2_Pin,
        ((pattern>>1)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_3_GPIO_Port, LATCH_3_Pin,
        ((pattern>>2)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_4_GPIO_Port, LATCH_4_Pin,
        ((pattern>>3)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_5_GPIO_Port, LATCH_5_Pin,
        ((pattern>>4)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_6_GPIO_Port, LATCH_6_Pin,
        ((pattern>>5)&1) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    HAL_GPIO_WritePin(LATCH_SELECT_1_GPIO_Port, LATCH_SELECT_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_SELECT_2_GPIO_Port, LATCH_SELECT_2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LATCH_SELECT_3_GPIO_Port, LATCH_SELECT_3_Pin, GPIO_PIN_SET);
}

// ── Private: wait N seconds via RTC semaphore ────────────────
static void WaitSeconds(uint32_t seconds)
{
    for (uint32_t i = 0; i < seconds; i++) {
        osSemaphoreAcquire(rtcTickSemaphore, osWaitForever);
    }
}

// ── Cycle plan struct ─────────────────────────────────────────
typedef struct {
    uint8_t  first;
    uint32_t p1Green;
    uint32_t p2Green;
} CyclePlan_t;

// ── Private: proportional timing from vehicle counts ─────────
static CyclePlan_t PlanFromDensity(const TrafficDensity_t *d)
{
    CyclePlan_t plan;
    plan.first = (d->panel1_count >= d->panel2_count) ? 1 : 2;

    uint16_t total = d->panel1_count + d->panel2_count;
    if (total == 0) {
        plan.p1Green = plan.p2Green = MIN_GREEN;
        return plan;
    }

    uint32_t budget = TOTAL_GREEN_BUDGET - 2u * MIN_GREEN;
    plan.p1Green = MIN_GREEN + ((uint32_t)d->panel1_count * budget) / total;
    plan.p2Green = TOTAL_GREEN_BUDGET - plan.p1Green;
    plan.p1Green = CLAMP(plan.p1Green, MIN_GREEN, MAX_GREEN);
    plan.p2Green = CLAMP(plan.p2Green, MIN_GREEN, MAX_GREEN);
    return plan;
}

// ── Private: RTC-based fallback ───────────────────────────────
static CyclePlan_t PlanFromRTC(void)
{
    CyclePlan_t plan = {1, 5, 5};
    if (RTC_IsRushHour()) {
        plan.p1Green = plan.p2Green = 8;
    }
    return plan;
}

// ── Private: run one full cycle ───────────────────────────────
static void RunCycle(const CyclePlan_t *plan)
{
    uint8_t  A      = plan->first;
    uint8_t  B      = (A == 1) ? 2 : 1;
    uint32_t Agreen = (A == 1) ? plan->p1Green : plan->p2Green;
    uint32_t Bgreen = (A == 1) ? plan->p2Green : plan->p1Green;

    uint8_t AGreen_bit  = (A == 1) ? P1_GREEN  : P2_GREEN;
    uint8_t AYellow_bit = (A == 1) ? P1_YELLOW : P2_YELLOW;
    uint8_t ARed_bit    = (A == 1) ? P1_RED    : P2_RED;
    uint8_t BGreen_bit  = (B == 1) ? P1_GREEN  : P2_GREEN;
    uint8_t BYellow_bit = (B == 1) ? P1_YELLOW : P2_YELLOW;
    uint8_t BRed_bit    = (B == 1) ? P1_RED    : P2_RED;

    // Phase 1: A GREEN
    gTrafficState.activePanel  = A;
    gTrafficState.greenSeconds = Agreen;
    gTrafficState.nightMode    = 0;
    strncpy((char*)gTrafficState.state, "green", 8);
    SetLights(AGreen_bit | BRed_bit);
    HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_SET);
    UART_SendLightStatus(A, JUNCTION_NUM);
    WaitSeconds(Agreen);

    // Phase 2: A YELLOW
    strncpy((char*)gTrafficState.state, "yellow", 8);
    SetLights(AYellow_bit | BRed_bit);
    UART_SendLightStatus(A, JUNCTION_NUM);
    WaitSeconds(YELLOW_SEC);

    // Phase 3: B GREEN
    gTrafficState.activePanel  = B;
    gTrafficState.greenSeconds = Bgreen;
    strncpy((char*)gTrafficState.state, "green", 8);
    SetLights(BGreen_bit | ARed_bit);
    HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_RESET);
    UART_SendLightStatus(B, JUNCTION_NUM);
    WaitSeconds(Bgreen);

    // Phase 4: B YELLOW
    strncpy((char*)gTrafficState.state, "yellow", 8);
    SetLights(BYellow_bit | ARed_bit);
    UART_SendLightStatus(B, JUNCTION_NUM);
    WaitSeconds(YELLOW_SEC);
}

// ── Private: night mode ───────────────────────────────────────
static void RunNightMode(void)
{
    gTrafficState.nightMode = 1;
    strncpy((char*)gTrafficState.state, "night", 8);

    gTrafficState.activePanel = 1;
    SetLights(P1_YELLOW | P2_RED);
    UART_SendLightStatus(1, JUNCTION_NUM);
    WaitSeconds(1);

    gTrafficState.activePanel = 2;
    SetLights(P1_RED | P2_YELLOW);
    UART_SendLightStatus(2, JUNCTION_NUM);
    WaitSeconds(1);
}

// ── Public ───────────────────────────────────────────────────
void Traffic_Init(void)
{
    SetLights(0x00);
    gTrafficState.healthOk = 1;
}

// ── FreeRTOS Task ─────────────────────────────────────────────
void TrafficManagerTask(void *argument)
{
    Traffic_Init();
    LOG_INFO("TRAFFIC", "TrafficManagerTask started");

    TrafficDensity_t density;
    CyclePlan_t plan;
    uint8_t noDataCounter = 0;
    uint8_t usingDummy    = 0;

    for (;;)
    {
        // ── Night mode check ──────────────────────────────────
        if (RTC_IsNight()) {
            if (!gTrafficState.nightMode) {
                LOG_INFO("TRAFFIC", "Night mode activated — yellow blink");
            }
            RunNightMode();
            continue;
        }
        gTrafficState.nightMode = 0;

        // ── Try Orange Pi YOLO data ───────────────────────────
        osStatus_t s = osMessageQueueGet(
            densityQueueHandle, &density, NULL, 0);

        if (s == osOK) {
            // Fresh data received
            if (usingDummy || noDataCounter > 0) {
                LOG_INFO("CV", "Orange Pi data restored — switching back to YOLO timing");
            }
            noDataCounter        = 0;
            usingDummy           = 0;
            gTrafficState.healthOk = 1;

            LOG_INFOF("CV", "YOLO data: A=%d vehicles, B=%d vehicles",
                      density.panel1_count, density.panel2_count);

            plan = PlanFromDensity(&density);

            LOG_INFOF("TRAFFIC", "Plan: Panel%d first, P1=%lus, P2=%lus",
                      plan.first, plan.p1Green, plan.p2Green);
        }
        else {
            // No data from Orange Pi
            noDataCounter++;

            if (noDataCounter >= NO_DATA_THRESHOLD) {
                // Switch to dummy data
                if (!usingDummy) {
                    LOG_WARNF("CV",
                        "No data from Orange Pi for %d cycles — "
                        "using dummy counts (A=%d, B=%d)",
                        NO_DATA_THRESHOLD,
                        DUMMY_PANEL1_COUNT,
                        DUMMY_PANEL2_COUNT);
                    usingDummy = 1;
                    gTrafficState.healthOk = 0;
                }

                TrafficDensity_t dummy = {
                    .panel1_count = DUMMY_PANEL1_COUNT,
                    .panel2_count = DUMMY_PANEL2_COUNT
                };
                plan = PlanFromDensity(&dummy);
            }
            else {
                // Grace period — use RTC
                LOG_WARNF("CV",
                    "No Orange Pi data (missed %d/%d) — RTC fallback",
                    noDataCounter, NO_DATA_THRESHOLD);

                plan = PlanFromRTC();
            }
        }

        RunCycle(&plan);
    }
}
