#ifndef UART_HANDLER_H
#define UART_HANDLER_H

/**
 * uart_handler.h
 * --------------
 * Two-way JSON UART communication with Orange Pi.
 *
 * Orange Pi → STM32:  {"p1":5,"p2":12}
 *   p1 = Panel 1 vehicle count (Direction A)
 *   p2 = Panel 2 vehicle count (Direction B)
 *
 * STM32 → Orange Pi:  {"status":"ok","green":1,"t":8}
 *   green = which panel is green (1 or 2)
 *   t     = green duration in seconds
 */

#include "main.h"
#include "cmsis_os.h"

// Traffic density data — passed via queue to TrafficManagerTask
typedef struct {
    uint16_t panel1_count;   // vehicle count from YOLO for Panel 1
    uint16_t panel2_count;   // vehicle count from YOLO for Panel 2
} TrafficDensity_t;

// Queue handle — TrafficManagerTask reads from this
extern osMessageQueueId_t densityQueueHandle;

/**
 * @brief  Init UART handler: create queue, start DMA reception.
 *         Call AFTER osKernelInitialize(), BEFORE osKernelStart().
 * @param  huart  Pointer to UART handle (huart1)
 */
void UART_Handler_Init(UART_HandleTypeDef *huart);

/**
 * @brief  FreeRTOS task — processes received JSON, sends ACK.
 *         Do NOT call directly. Pass to osThreadNew().
 */
void UARTHandlerTask(void *argument);

/**
 * @brief  Call from HAL_UARTEx_RxEventCallback when huart == huart1.
 *         Handles idle-line detection from DMA.
 */
void UART_Handler_RxEvent(UART_HandleTypeDef *huart, uint16_t size);

#endif /* UART_HANDLER_H */
