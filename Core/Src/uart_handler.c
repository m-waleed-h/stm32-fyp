/**
 * uart_handler.c
 * --------------
 * Two-way JSON UART communication with Orange Pi.
 *
 * STM32 → Orange Pi (TX):
 *   1. {"type":"light-status-update","data":{...}}   — on each phase change
 *   2. {"type":"microcontroller_health_update","data":{...}} — every 5 seconds
 *
 * Orange Pi → STM32 (RX):
 *   {"p1":5,"p2":12}  — YOLO vehicle counts
 */

#include "uart_handler.h"
#include "traffic_controller.h"
#include "rtc_manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================
// Private config
// ============================================================
#define RX_BUFFER_SIZE    128
#define TX_BUFFER_SIZE    320   // larger — full JSON needed
#define QUEUE_CAPACITY      4
#define HEALTH_SEND_EVERY   5  // seconds between health updates

// ============================================================
// Private state
// ============================================================
static UART_HandleTypeDef *_huart;
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static uint8_t txBuffer[TX_BUFFER_SIZE];

osMessageQueueId_t densityQueueHandle;

static uint32_t healthCounter = 0;

// ============================================================
// Private: parse {"p1":5,"p2":12} from Orange Pi
// ============================================================
static int ParseDensityJSON(const char *buf, TrafficDensity_t *out)
{
    int p1 = 0, p2 = 0;
    int matched = sscanf(buf, "{\"p1\":%d,\"p2\":%d}", &p1, &p2);
    if (matched == 2 && p1 >= 0 && p2 >= 0) {
        out->panel1_count = (uint16_t)p1;
        out->panel2_count = (uint16_t)p2;
        return 1;
    }
    return 0;
}

// ============================================================
// Private: send raw JSON string over UART
// ============================================================
static void SendJSON(const char *json, uint16_t len)
{
    HAL_UART_Transmit(_huart, (uint8_t *)json, len, 200);
}

// ============================================================
// Public: Send light status update — call on each phase change
// Format matches Orange Pi's "light-status-update" handler
// ============================================================
void UART_SendLightStatus(uint8_t lightNum, uint8_t junctionNum)
{
    RTC_TimeDate t;
    RTC_GetCurrentTime(&t);

    // Determine which lights are ON based on current state
    const char *red    = "OFF";
    const char *yellow = "OFF";
    const char *green  = "OFF";

    if (strncmp((char*)gTrafficState.state, "green", 5) == 0)       green  = "ON";
    else if (strncmp((char*)gTrafficState.state, "yellow", 6) == 0) yellow = "ON";
    else if (strncmp((char*)gTrafficState.state, "night", 5) == 0)  yellow = "ON";
    else                                                              red    = "ON";

    int len = snprintf(
        (char *)txBuffer, TX_BUFFER_SIZE,
        "{\"type\":\"light-status-update\",\"data\":{"
        "\"light-num\":%d,"
        "\"junction-num\":%d,"
        "\"status\":{\"red\":\"%s\",\"yellow\":\"%s\",\"green\":\"%s\"},"
        "\"timings\":{\"red-time\":\"0\",\"yellow-time\":\"2\",\"green-time\":\"%lu\"},"
        "\"current-time\":{\"hour\":%d,\"minute\":%d,\"second\":%d,"
        "\"day\":%d,\"month\":%d,\"year\":%d}"
        "}}\n",
        lightNum, junctionNum,
        red, yellow, green,
        gTrafficState.greenSeconds,
        t.hours, t.minutes, t.seconds,
        t.date, t.month, (2000 + t.year)
    );

    if (len > 0 && len < TX_BUFFER_SIZE) {
        SendJSON((char *)txBuffer, (uint16_t)len);
    }
}

// ============================================================
// Public: Send MCU health update — called every 5 seconds
// ============================================================
void UART_SendMCUHealth(void)
{
    RTC_TimeDate t;
    RTC_GetCurrentTime(&t);

    int len = snprintf(
        (char *)txBuffer, TX_BUFFER_SIZE,
        "{\"type\":\"microcontroller_health_update\",\"data\":{"
        "\"device-serial\":\"STM32-FYP-001\","
        "\"status\":\"%s\","
        "\"rtc-time\":{\"hour\":%d,\"minute\":%d,\"second\":%d},"
        "\"rtc-status\":\"ok\","
        "\"oled-status\":\"ok\","
        "\"serial-status\":\"connected\","
        "\"number-of-lights-connected\":2"
        "}}\n",
        gTrafficState.healthOk ? "ok" : "error",
        t.hours, t.minutes, t.seconds
    );

    if (len > 0 && len < TX_BUFFER_SIZE) {
        SendJSON((char *)txBuffer, (uint16_t)len);
    }
}

// ============================================================
// Public: Init — create queue + start DMA RX
// ============================================================
void UART_Handler_Init(UART_HandleTypeDef *huart)
{
    _huart = huart;

    densityQueueHandle = osMessageQueueNew(QUEUE_CAPACITY,
                                            sizeof(TrafficDensity_t),
                                            NULL);

    HAL_UARTEx_ReceiveToIdle_DMA(_huart, rxBuffer, RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(_huart->hdmarx, DMA_IT_HT);
}

// ============================================================
// Public: called from HAL_UARTEx_RxEventCallback (ISR)
// ============================================================
void UART_Handler_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART1) return;

    if (size < RX_BUFFER_SIZE) rxBuffer[size] = '\0';

    TrafficDensity_t density = {0};
    if (ParseDensityJSON((char *)rxBuffer, &density)) {
        osMessageQueuePut(densityQueueHandle, &density, 0, 0);
    }

    HAL_UARTEx_ReceiveToIdle_DMA(_huart, rxBuffer, RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(_huart->hdmarx, DMA_IT_HT);
}

// ============================================================
// Public: FreeRTOS Task — periodic health TX + density drain
// Light status TX is called from traffic_controller.c directly
// ============================================================
void UARTHandlerTask(void *argument)
{
    TrafficDensity_t density;
    healthCounter = 0;

    for (;;)
    {
        // Drain incoming density queue
        // (TrafficManagerTask reads it too — this just keeps it clean)
        osMessageQueueGet(densityQueueHandle, &density, NULL, 0);

        // Send MCU health every HEALTH_SEND_EVERY seconds
        healthCounter++;

        // Low power night mode --> Health Status every 30s instead of 5s
        uint32_t healthInterval = gTrafficState.nightMode ? 30 : 5;

        if (healthCounter >= healthInterval) {
            healthCounter = 0;
            UART_SendMCUHealth();
        }

        // Wait 1 second using RTC tick
        osSemaphoreAcquire(rtcTickSemaphore, pdMS_TO_TICKS(1500));
    }
}
