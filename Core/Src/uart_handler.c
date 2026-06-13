/**
 * uart_handler.c
 * --------------
 * Two-way JSON UART communication with Orange Pi via DMA + Idle detection.
 *
 * Reception flow:
 *   HAL_UARTEx_ReceiveToIdle_DMA() runs continuously in circular mode.
 *   On idle line (end of JSON message), HAL_UARTEx_RxEventCallback fires.
 *   UART_Handler_RxEvent() is called → parses JSON → puts in densityQueue.
 *   UARTHandlerTask wakes up, sends ACK back to Orange Pi.
 *
 * TrafficManagerTask reads densityQueue and adjusts green timing.
 */

#include "uart_handler.h"
#include <string.h>
#include <stdio.h>

// ============================================================
// Private config
// ============================================================

#define RX_BUFFER_SIZE   64    // max incoming JSON size
#define TX_BUFFER_SIZE   64    // max outgoing JSON size
#define QUEUE_CAPACITY    4    // max pending density messages

// ============================================================
// Private state
// ============================================================

static UART_HandleTypeDef *_huart;
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static uint8_t txBuffer[TX_BUFFER_SIZE];

// Queue: uart task puts data here, traffic task reads from here
osMessageQueueId_t densityQueueHandle;

// Notify uart task that a message arrived (from ISR context)
static osThreadId_t uartTaskHandle_internal = NULL;

// ============================================================
// Private: parse JSON from Orange Pi
// Format: {"p1":5,"p2":12}
// ============================================================

static int ParseDensityJSON(const char *buf, TrafficDensity_t *out)
{
    int p1 = 0, p2 = 0;
    // sscanf handles the fixed JSON format safely
    int matched = sscanf(buf, "{\"p1\":%d,\"p2\":%d}", &p1, &p2);

    if (matched == 2 && p1 >= 0 && p2 >= 0) {
        out->panel1_count = (uint16_t)p1;
        out->panel2_count = (uint16_t)p2;
        return 1;  // success
    }
    return 0;  // parse failed
}

// ============================================================
// Private: send JSON ACK to Orange Pi
// Format: {"status":"ok","green":1,"t":8}
// ============================================================

static void SendACK(uint8_t greenPanel, uint32_t greenSeconds)
{
    int len = snprintf((char *)txBuffer, TX_BUFFER_SIZE,
                       "{\"status\":\"ok\",\"green\":%d,\"t\":%lu}\n",
                       greenPanel, greenSeconds);

    if (len > 0 && len < TX_BUFFER_SIZE) {
        HAL_UART_Transmit(_huart, txBuffer, (uint16_t)len, 100);
    }
}

// ============================================================
// Public: Init — call after osKernelInitialize()
// ============================================================

void UART_Handler_Init(UART_HandleTypeDef *huart)
{
    _huart = huart;

    // Create queue — holds up to QUEUE_CAPACITY density messages
    densityQueueHandle = osMessageQueueNew(
        QUEUE_CAPACITY,
        sizeof(TrafficDensity_t),
        NULL
    );

    // Start DMA reception — runs continuously (circular mode)
    // Callback fires on idle line OR buffer full
    HAL_UARTEx_ReceiveToIdle_DMA(_huart, rxBuffer, RX_BUFFER_SIZE);

    // Disable DMA half-transfer interrupt (we only want idle/complete)
    __HAL_DMA_DISABLE_IT(_huart->hdmarx, DMA_IT_HT);
}

// ============================================================
// Public: called from HAL_UARTEx_RxEventCallback (ISR context)
// ============================================================

void UART_Handler_RxEvent(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART1) return;

    // Null-terminate for safe string parsing
    if (size < RX_BUFFER_SIZE) rxBuffer[size] = '\0';

    TrafficDensity_t density = {0};

    if (ParseDensityJSON((char *)rxBuffer, &density)) {
        // Put parsed data in queue (non-blocking from ISR)
        osMessageQueuePut(densityQueueHandle, &density, 0, 0);
    }

    // Restart DMA reception for next message
    HAL_UARTEx_ReceiveToIdle_DMA(_huart, rxBuffer, RX_BUFFER_SIZE);
    __HAL_DMA_DISABLE_IT(_huart->hdmarx, DMA_IT_HT);
}

// ============================================================
// Public: FreeRTOS Task
// Waits for density data, sends ACK back to Orange Pi
// ============================================================

void UARTHandlerTask(void *argument)
{
    uartTaskHandle_internal = osThreadGetId();
    TrafficDensity_t density;

    for (;;)
    {
        // Block until a density message arrives (max 10 sec timeout)
        osStatus_t status = osMessageQueueGet(
            densityQueueHandle,
            &density,
            NULL,
            pdMS_TO_TICKS(10000)
        );

        if (status == osOK) {
            // Decide which panel gets green based on vehicle count
            uint8_t greenPanel;
            uint32_t greenSeconds;

            if (density.panel1_count >= density.panel2_count) {
                greenPanel  = 1;
                // More vehicles = more green time (clamped 4–12 sec)
                greenSeconds = 4 + (density.panel1_count / 3);
                if (greenSeconds > 12) greenSeconds = 12;
            } else {
                greenPanel  = 2;
                greenSeconds = 4 + (density.panel2_count / 3);
                if (greenSeconds > 12) greenSeconds = 12;
            }

            // Send ACK to Orange Pi
            SendACK(greenPanel, greenSeconds);

            // Note: TrafficManagerTask reads directly from densityQueue
            // No extra signaling needed — queue acts as the channel
        }
        // If timeout: Orange Pi not sending — traffic uses RTC-based timing
    }
}
