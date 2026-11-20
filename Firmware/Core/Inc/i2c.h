/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c1;

/* USER CODE BEGIN Private defines */
/* Application I2C API: HAL owns the I2C1 hardware state machine.
 * Sync APIs exist for legacy sensors; async APIs return immediately.
 * All 7-bit addresses passed to these APIs must be left-shifted by 1.
 */
#define I2C_OK 0u
#define I2C_ERROR 1u
#define I2C_ASYNC_PENDING 0u
#define I2C_ASYNC_DONE 1u
#define I2C_ASYNC_ERROR 2u
/* USER CODE END Private defines */

void MX_I2C1_Init(void);

/* USER CODE BEGIN Prototypes */
uint8_t I2C_TransactionBegin(void);
void I2C_TransactionEnd(void);
uint8_t I2C_BusIsLocked(void);
/* Compatibility: legacy blocking callers only. Never invoke from ISR. */
uint8_t I2C_RegWrite(uint16_t address, uint8_t register_addr, const uint8_t *data, uint16_t count);
uint8_t I2C_RegRead(uint16_t address, uint8_t register_addr, uint8_t *data, uint16_t count);
uint8_t I2C_Transmit(uint16_t address, const uint8_t *data, uint16_t count);
uint8_t I2C_Receive(uint16_t address, uint8_t *data, uint16_t count);
uint8_t I2C_IsReady(uint16_t address);

/* Nonblocking: 1=accepted, 0=busy; flag 0=pending, 1=done, 2=error.
 * Buffer/flag storage MUST survive coroutine yields and IRQ completion. */
uint8_t ASYNC_I2C_Transmit(uint16_t address, uint8_t reg, uint8_t *data, uint16_t count, uint8_t priority, uint8_t *flag);
uint8_t ASYNC_I2C_Receive(uint16_t address, uint8_t reg, uint8_t *data, uint16_t count, uint8_t priority, uint8_t *flag);
uint8_t I2C_AsyncProbe(uint16_t address, uint8_t *flag);
void I2C_AsyncService(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */

