/**
 * traffic_controller.c — Final Version
 * -------------------------------------
 * Timing priority:
 *   1. Orange Pi YOLO density → proportional green time
 *   2. RTC time-of-day fallback → rush hour / normal
 *   3. Night mode (23:00–05:00) → yellow blink both panels
 *
 * TX to Orange Pi:
 *   UART_SendLightStatus() called on every phase change
 *   Format: {"type":"light-status-update","data":{...}}
 */

#include "traffic_controller.h"
#include "rtc_manager.h"
#include "uart_handler.h"
#include <string.h>

// ============================================================
// Pin bit definitions
// ============================================================
#define P1_RED    (1 << 0)   // LATCH_1 PB4
#define P1_YELLOW (1 << 1)   // LATCH_2 PB5
#define P1_GREEN  (1 << 2)   // LATCH_3 PB0
#define P2_RED    (1 << 3)   // LATCH_4 PB1
#define P2_YELLOW (1 << 4)   // LATCH_5 PA1
#define P2_GREEN  (1 << 5)   // LATCH_6 PA0

// ============================================================
// Timing constants
// ============================================================
#define TOTAL_GREEN_BUDGET   20u
#define MIN_GREEN             4u
#define MAX_GREEN            14u
#define YELLOW_SEC            2u

#define CLAMP(x,lo,hi) ((x)<(lo)?(lo):((x)>(hi)?(hi):(x)))

// Junction number — change if needed
#define JUNCTION_NUM   1

// ============================================================
// Shared state — read by uart_handler to build status JSON
// ============================================================
volatile TrafficState_t gTrafficState = {
    .activePanel  = 1,
    .state        = "red",
    .greenSeconds = 0,
    .healthOk     = 1,
    .nightMode    = 0
};

// ============================================================
// Private: write light pattern to TC74HC573 latch
// ============================================================
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

    // Enable CBB1 only (OE# = LOW = active)
    HAL_GPIO_WritePin(LATCH_SELECT_1_GPIO_Port, LATCH_SELECT_1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LATCH_SELECT_2_GPIO_Port, LATCH_SELECT_2_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LATCH_SELECT_3_GPIO_Port, LATCH_SELECT_3_Pin, GPIO_PIN_SET);
}

// ============================================================
// Private: wait N real seconds via RTC 1Hz semaphore
// ============================================================
static void WaitSeconds(uint32_t seconds)
{
    for (uint32_t i = 0; i < seconds; i++) {
        osSemaphoreAcquire(rtcTickSemaphore, osWaitForever);
    }
}

// ============================================================
// Private: compute proportional timing from YOLO counts
// ============================================================
typedef struct {
    uint8_t  first;
    uint32_t p1Green;
    uint32_t p2Green;
} CyclePlan_t;

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

static CyclePlan_t PlanFromRTC(void)
{
    CyclePlan_t plan = {1, 5, 5};
    if (RTC_IsRushHour()) {
        plan.p1Green = plan.p2Green = 8;
    }
    return plan;
}

// ============================================================
// Private: run one full green/yellow cycle
// ============================================================
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

    // --------------------------------------------------
    // Phase 1: A GREEN, B RED
    // --------------------------------------------------
    gTrafficState.activePanel  = A;
    gTrafficState.greenSeconds = Agreen;
    gTrafficState.nightMode    = 0;
    strncpy((char*)gTrafficState.state, "green", 8);

    SetLights(AGreen_bit | BRed_bit);
    HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_SET);
    UART_SendLightStatus(A, JUNCTION_NUM);   // → Orange Pi

    WaitSeconds(Agreen);

    // --------------------------------------------------
    // Phase 2: A YELLOW, B RED
    // --------------------------------------------------
    strncpy((char*)gTrafficState.state, "yellow", 8);

    SetLights(AYellow_bit | BRed_bit);
    UART_SendLightStatus(A, JUNCTION_NUM);   // → Orange Pi

    WaitSeconds(YELLOW_SEC);

    // --------------------------------------------------
    // Phase 3: B GREEN, A RED
    // --------------------------------------------------
    gTrafficState.activePanel  = B;
    gTrafficState.greenSeconds = Bgreen;
    strncpy((char*)gTrafficState.state, "green", 8);

    SetLights(BGreen_bit | ARed_bit);
    HAL_GPIO_WritePin(LED_PIN_GPIO_Port, LED_PIN_Pin, GPIO_PIN_RESET);
    UART_SendLightStatus(B, JUNCTION_NUM);   // → Orange Pi

    WaitSeconds(Bgreen);

    // --------------------------------------------------
    // Phase 4: B YELLOW, A RED
    // --------------------------------------------------
    strncpy((char*)gTrafficState.state, "yellow", 8);

    SetLights(BYellow_bit | ARed_bit);
    UART_SendLightStatus(B, JUNCTION_NUM);   // → Orange Pi

    WaitSeconds(YELLOW_SEC);
}

// ============================================================
// Private: night mode — both panels blink yellow alternately
// ============================================================
static void RunNightMode(void)
{
    gTrafficState.nightMode = 1;
    strncpy((char*)gTrafficState.state, "night", 8);

    // Panel 1 yellow
    gTrafficState.activePanel = 1;
    SetLights(P1_YELLOW | P2_RED);
    UART_SendLightStatus(1, JUNCTION_NUM);   // → Orange Pi
    WaitSeconds(1);

    // Panel 2 yellow
    gTrafficState.activePanel = 2;
    SetLights(P1_RED | P2_YELLOW);
    UART_SendLightStatus(2, JUNCTION_NUM);   // → Orange Pi
    WaitSeconds(1);
}

// ============================================================
// Public
// ============================================================
void Traffic_Init(void)
{
    SetLights(0x00);
    gTrafficState.healthOk = 1;
}

void TrafficManagerTask(void *argument)
{
    Traffic_Init();
    TrafficDensity_t density;
    CyclePlan_t plan;

    for (;;)
    {
        // Night mode check first
        if (RTC_IsNight()) {
            RunNightMode();
            continue;
        }
        gTrafficState.nightMode = 0;

        // Try to get fresh YOLO density data (non-blocking)
        osStatus_t s = osMessageQueueGet(densityQueueHandle, &density, NULL, 0);

        if (s == osOK) {
            plan = PlanFromDensity(&density);   // YOLO-based
        } else {
            plan = PlanFromRTC();               // RTC fallback
        }

        RunCycle(&plan);
    }
}
