/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.h
  * @brief   This file contains all the function prototypes for
  *          the adc.c file
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
#ifndef __ADC_H__
#define __ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_ADC1_Init(void);

/* USER CODE BEGIN Prototypes */
typedef enum {
    ADC1_SCAN_IDLE = 0,
    ADC1_SCAN_BUSY,
    ADC1_SCAN_DONE,
    ADC1_SCAN_ERROR
} ADC1_ScanStatus;

uint8_t ADC1_Calibrate(void);
/* The four halfwords must remain writable until completion or stop. */
uint8_t ADC1_ScanStart(uint16_t samples[4]);
ADC1_ScanStatus ADC1_ScanState(void);
uint8_t ADC1_ScanStop(void);
void ADC1_DmaIRQ(void);
/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __ADC_H__ */

