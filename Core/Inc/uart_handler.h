#ifndef UART_HANDLER_H
#define UART_HANDLER_H

/**
 * uart_handler.h
 * --------------
 * STM32 ↔ Orange Pi UART communication.
 *
 * Orange Pi → STM32:  {"p1":5,"p2":12}
 * STM32 → Orange Pi:  {"type":"light-status-update","data":{...}}
 *                      {"type":"microcontroller_health_update","data":{...}}
 */

#include "main.h"
#include "cmsis_os.h"

// Density data from Orange Pi YOLO
typedef struct {
    uint16_t panel1_count;
    uint16_t panel2_count;
} TrafficDensity_t;

// Queue: ISR puts data here, TrafficManagerTask reads
extern osMessageQueueId_t densityQueueHandle;

// Init: create queue + start DMA RX
void UART_Handler_Init(UART_HandleTypeDef *huart);

// ISR callback — call from HAL_UARTEx_RxEventCallback
void UART_Handler_RxEvent(UART_HandleTypeDef *huart, uint16_t size);

// TX functions — called from traffic_controller.c on phase change
void UART_SendLightStatus(uint8_t lightNum, uint8_t junctionNum);

// TX health — called from UARTHandlerTask every 5 seconds
void UART_SendMCUHealth(void);

// FreeRTOS task
void UARTHandlerTask(void *argument);

#endif
