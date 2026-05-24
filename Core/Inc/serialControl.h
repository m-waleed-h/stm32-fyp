/*
 * serialControl.h
 *
 *  Created on: Jan 11, 2026
 *      Author: waleed
 */

#ifndef INC_SERIALCONTROL_H_
#define INC_SERIALCONTROL_H_

#include "stm32f4xx_hal.h"

/* Exported function prototypes */
HAL_StatusTypeDef UART_Transmit_DMA(uint8_t *data, uint16_t size);

#endif /* INC_SERIALCONTROL_H_ */
