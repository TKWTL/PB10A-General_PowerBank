/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

#include "stm32g0xx_ll_adc.h"
#include "stm32g0xx_ll_crc.h"
#include "stm32g0xx_ll_dma.h"
#include "stm32g0xx_ll_i2c.h"
#include "stm32g0xx_ll_rcc.h"
#include "stm32g0xx_ll_bus.h"
#include "stm32g0xx_ll_system.h"
#include "stm32g0xx_ll_exti.h"
#include "stm32g0xx_ll_cortex.h"
#include "stm32g0xx_ll_utils.h"
#include "stm32g0xx_ll_pwr.h"
#include "stm32g0xx_ll_spi.h"
#include "stm32g0xx_ll_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* Keep the runtime bindings aligned with the CubeMX .ioc file. */
#define PB_SPI_INSTANCE SPI1
#define PB_SPI_INIT MX_SPI1_Init
#define PB_SPI_TX_DMA_INSTANCE DMA1
#define PB_SPI_TX_DMA_CHANNEL LL_DMA_CHANNEL_1
#define PB_SPI_TX_DMA_IRQ DMA1_Channel1_IRQn

#define PB_I2C_INSTANCE I2C1

#define PB_ADC_INSTANCE ADC1
#define PB_ADC_COMMON __LL_ADC_COMMON_INSTANCE(PB_ADC_INSTANCE)
#define PB_ADC_DMA_INSTANCE DMA1
#define PB_ADC_DMA_CHANNEL LL_DMA_CHANNEL_2
#define PB_ADC_DMA_IRQ DMA1_Channel2_3_IRQn

/* Zero-based offsets in the DMA buffer, in CubeMX regular rank order. */
#define PB_ADC_AUX_CURRENT_SLOT 0u
#define PB_ADC_AUX_VOLTAGE_SLOT 1u
#define PB_ADC_TEMPERATURE_SLOT 2u
#define PB_ADC_VREF_SLOT 3u
#define PB_ADC_SCAN_COUNT 4u

/* STM32G0 packs four ISR/IFCR bits per zero-based LL DMA channel. */
#define PB_DMA_CHANNEL_FLAGS(channel, flags_ch1) ((flags_ch1) << (4u * (channel)))
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SUB_IOUT_Pin LL_GPIO_PIN_0
#define SUB_IOUT_GPIO_Port GPIOA
#define D_C_Pin LL_GPIO_PIN_3
#define D_C_GPIO_Port GPIOA
#define SUB_VOUT_Pin LL_GPIO_PIN_5
#define SUB_VOUT_GPIO_Port GPIOA
#define SUB_EN_Pin LL_GPIO_PIN_6
#define SUB_EN_GPIO_Port GPIOA
#define SUB_INDET_Pin LL_GPIO_PIN_7
#define SUB_INDET_GPIO_Port GPIOA
#define SUB_INDET_EXTI_IRQn EXTI4_15_IRQn
#define MIC_INDET_Pin LL_GPIO_PIN_0
#define MIC_INDET_GPIO_Port GPIOB
#define MIC_INDET_EXTI_IRQn EXTI0_1_IRQn
#define BEEP_Pin LL_GPIO_PIN_1
#define BEEP_GPIO_Port GPIOB
#define CPEN_Pin LL_GPIO_PIN_2
#define CPEN_GPIO_Port GPIOB
#define UP_Pin LL_GPIO_PIN_9
#define UP_GPIO_Port GPIOA
#define UP_EXTI_IRQn EXTI4_15_IRQn
#define RIGHT_Pin LL_GPIO_PIN_6
#define RIGHT_GPIO_Port GPIOC
#define RIGHT_EXTI_IRQn EXTI4_15_IRQn
#define DOWN_Pin LL_GPIO_PIN_10
#define DOWN_GPIO_Port GPIOA
#define DOWN_EXTI_IRQn EXTI4_15_IRQn
#define LED_PWM_Pin LL_GPIO_PIN_11
#define LED_PWM_GPIO_Port GPIOA
#define CONF_Pin LL_GPIO_PIN_12
#define CONF_GPIO_Port GPIOA
#define CONF_EXTI_IRQn EXTI4_15_IRQn
#define LEFT_Pin LL_GPIO_PIN_15
#define LEFT_GPIO_Port GPIOA
#define LEFT_EXTI_IRQn EXTI4_15_IRQn
#define INT_Pin LL_GPIO_PIN_5
#define INT_GPIO_Port GPIOB
#define INT_EXTI_IRQn EXTI4_15_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
