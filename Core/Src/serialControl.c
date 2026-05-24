/*
 * serialControl.c
 *
 *  Created on: Jan 11, 2026
 *      Author: waleed
 */

#include "serialControl.h"
#include "main.h"

/* External variables */
extern UART_HandleTypeDef huart1;

/* Private variables */
static volatile uint8_t isSent = 1;

/**
 * @brief  UART transmission complete callback
 * @param  huart: UART handle
 * @retval None
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1) {
    isSent = 1;
  }
}

/**
 * @brief  Transmit data over UART using DMA
 * @param  data: Pointer to the data buffer to transmit
 * @param  size: Number of bytes to transmit
 * @retval HAL status (HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT)
 */
HAL_StatusTypeDef UART_Transmit_DMA(uint8_t *data, uint16_t size)
{
  // Check if UART is busy
  if (isSent == 0) {
    return HAL_BUSY;
  }
  
  // Mark as busy
  isSent = 0;
  
  // Transmit data using DMA
  HAL_StatusTypeDef status = HAL_UART_Transmit_DMA(&huart1, data, size);
  
  // If transmission failed, mark as available again
  if (status != HAL_OK) {
    isSent = 1;
  }
  
  return status;
}
