/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.h
  * @brief   This file contains all the function prototypes for
  *          the spi.c file
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
#ifndef __SPI_H__
#define __SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_SPI1_Init(void);

/* USER CODE BEGIN Prototypes */
/* Completion is reported after foreground service verifies SPI is idle.
 * Keep the DMA buffer valid until DONE or until abort returns to IDLE.
 */
typedef enum {
    SPI_DMA_IDLE=0,
    SPI_DMA_BUSY,
    SPI_DMA_DONE,
    SPI_DMA_ERROR,
    SPI_DMA_ABORTING
} SPI_DmaState;
uint8_t SPI_DmaTxStart(const uint8_t *bytes, uint16_t count);
void SPI_DmaTxIRQ(void);
void SPI_DmaTxService(void);
SPI_DmaState SPI_DmaTxState(void);
uint8_t SPI_DmaTxRelease(void);
uint8_t SPI_DmaTxAbortAsync(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __SPI_H__ */

