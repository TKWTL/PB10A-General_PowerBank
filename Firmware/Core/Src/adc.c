/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
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
/* Includes ------------------------------------------------------------------*/
#include "adc.h"

/* USER CODE BEGIN 0 */
#include "millis.h"
/* USER CODE END 0 */

/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  LL_ADC_InitTypeDef ADC_InitStruct = {0};
  LL_ADC_REG_InitTypeDef ADC_REG_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetADCClockSource(LL_RCC_ADC_CLKSOURCE_HSI);

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_ADC);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  /**ADC1 GPIO Configuration
  PA0   ------> ADC1_IN0
  PA5   ------> ADC1_IN5
  */
  GPIO_InitStruct.Pin = SUB_IOUT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(SUB_IOUT_GPIO_Port, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = SUB_VOUT_Pin;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ANALOG;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  LL_GPIO_Init(SUB_VOUT_GPIO_Port, &GPIO_InitStruct);

  /* ADC1 DMA Init */

  /* ADC1 Init */
  LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_2, LL_DMAMUX_REQ_ADC1);

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_2, LL_DMA_DIRECTION_PERIPH_TO_MEMORY);

  LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MODE_NORMAL);

  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_2, LL_DMA_PDATAALIGN_HALFWORD);

  LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_2, LL_DMA_MDATAALIGN_HALFWORD);

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */

   #define ADC_CHANNEL_CONF_RDY_TIMEOUT_MS ( 1U)
   #if (USE_TIMEOUT == 1)
   uint32_t Timeout ; /* Variable used for Timeout management */
   #endif /* USE_TIMEOUT */

  ADC_InitStruct.Resolution = LL_ADC_RESOLUTION_12B;
  ADC_InitStruct.DataAlignment = LL_ADC_DATA_ALIGN_RIGHT;
  ADC_InitStruct.LowPowerMode = LL_ADC_LP_MODE_NONE;
  LL_ADC_Init(ADC1, &ADC_InitStruct);
  LL_ADC_REG_SetSequencerConfigurable(ADC1, LL_ADC_REG_SEQ_CONFIGURABLE);

   /* Poll for ADC channel configuration ready */
   #if (USE_TIMEOUT == 1)
   Timeout = ADC_CHANNEL_CONF_RDY_TIMEOUT_MS;
   #endif /* USE_TIMEOUT */
   while (LL_ADC_IsActiveFlag_CCRDY(ADC1) == 0)
     {
   #if (USE_TIMEOUT == 1)
   /* Check Systick counter flag to decrement the time-out value */
   if (LL_SYSTICK_IsActiveCounterFlag())
     {
   if(Timeout-- == 0)
         {
   Error_Handler();
         }
     }
   #endif /* USE_TIMEOUT */
     }
   /* Clear flag ADC channel configuration ready */
   LL_ADC_ClearFlag_CCRDY(ADC1);
  ADC_REG_InitStruct.TriggerSource = LL_ADC_REG_TRIG_SOFTWARE;
  ADC_REG_InitStruct.SequencerLength = LL_ADC_REG_SEQ_SCAN_ENABLE_4RANKS;
  ADC_REG_InitStruct.SequencerDiscont = LL_ADC_REG_SEQ_DISCONT_DISABLE;
  ADC_REG_InitStruct.ContinuousMode = LL_ADC_REG_CONV_SINGLE;
  ADC_REG_InitStruct.DMATransfer = LL_ADC_REG_DMA_TRANSFER_LIMITED;
  ADC_REG_InitStruct.Overrun = LL_ADC_REG_OVR_DATA_PRESERVED;
  LL_ADC_REG_Init(ADC1, &ADC_REG_InitStruct);
  LL_ADC_SetCommonClock(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_CLOCK_ASYNC_DIV1);
  LL_ADC_SetOverSamplingScope(ADC1, LL_ADC_OVS_DISABLE);
  LL_ADC_SetTriggerFrequencyMode(ADC1, LL_ADC_CLOCK_FREQ_MODE_HIGH);
  LL_ADC_SetCommonPathInternalCh(__LL_ADC_COMMON_INSTANCE(ADC1), LL_ADC_PATH_INTERNAL_VREFINT|LL_ADC_PATH_INTERNAL_TEMPSENSOR);
  LL_ADC_SetSamplingTimeCommonChannels(ADC1, LL_ADC_SAMPLINGTIME_COMMON_1, LL_ADC_SAMPLINGTIME_39CYCLES_5);
  LL_ADC_SetSamplingTimeCommonChannels(ADC1, LL_ADC_SAMPLINGTIME_COMMON_2, LL_ADC_SAMPLINGTIME_79CYCLES_5);
  LL_ADC_DisableIT_EOC(ADC1);
  LL_ADC_DisableIT_EOS(ADC1);

   /* Enable ADC internal voltage regulator */
   LL_ADC_EnableInternalRegulator(ADC1);
   /* Delay for ADC internal voltage regulator stabilization. */
   /* Compute number of CPU cycles to wait for, from delay in us. */
   /* Note: Variable divided by 2 to compensate partially */
   /* CPU processing cycles (depends on compilation optimization). */
   /* Note: If system core clock frequency is below 200kHz, wait time */
   /* is only a few CPU processing cycles. */
   __IO uint32_t wait_loop_index;
   wait_loop_index = ((LL_ADC_DELAY_INTERNAL_REGUL_STAB_US * (SystemCoreClock / (100000 * 2))) / 10);
   while(wait_loop_index != 0)
     {
   wait_loop_index--;
     }

  /** Configure Regular Channel
  */
  LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_1, LL_ADC_CHANNEL_0);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_0, LL_ADC_SAMPLINGTIME_COMMON_1);

  /** Configure Regular Channel
  */
  LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_2, LL_ADC_CHANNEL_5);

   /* Poll for ADC channel configuration ready */
   #if (USE_TIMEOUT == 1)
   Timeout = ADC_CHANNEL_CONF_RDY_TIMEOUT_MS;
   #endif /* USE_TIMEOUT */
   while (LL_ADC_IsActiveFlag_CCRDY(ADC1) == 0)
     {
   #if (USE_TIMEOUT == 1)
   /* Check Systick counter flag to decrement the time-out value */
   if (LL_SYSTICK_IsActiveCounterFlag())
     {
   if(Timeout-- == 0)
         {
   Error_Handler();
         }
     }
   #endif /* USE_TIMEOUT */
     }
   /* Clear flag ADC channel configuration ready */
   LL_ADC_ClearFlag_CCRDY(ADC1);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_5, LL_ADC_SAMPLINGTIME_COMMON_1);

  /** Configure Regular Channel
  */
  LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_3, LL_ADC_CHANNEL_TEMPSENSOR);

   /* Poll for ADC channel configuration ready */
   #if (USE_TIMEOUT == 1)
   Timeout = ADC_CHANNEL_CONF_RDY_TIMEOUT_MS;
   #endif /* USE_TIMEOUT */
   while (LL_ADC_IsActiveFlag_CCRDY(ADC1) == 0)
     {
   #if (USE_TIMEOUT == 1)
   /* Check Systick counter flag to decrement the time-out value */
   if (LL_SYSTICK_IsActiveCounterFlag())
     {
   if(Timeout-- == 0)
         {
   Error_Handler();
         }
     }
   #endif /* USE_TIMEOUT */
     }
   /* Clear flag ADC channel configuration ready */
   LL_ADC_ClearFlag_CCRDY(ADC1);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_TEMPSENSOR, LL_ADC_SAMPLINGTIME_COMMON_2);

  /** Configure Regular Channel
  */
  LL_ADC_REG_SetSequencerRanks(ADC1, LL_ADC_REG_RANK_4, LL_ADC_CHANNEL_VREFINT);

   /* Poll for ADC channel configuration ready */
   #if (USE_TIMEOUT == 1)
   Timeout = ADC_CHANNEL_CONF_RDY_TIMEOUT_MS;
   #endif /* USE_TIMEOUT */
   while (LL_ADC_IsActiveFlag_CCRDY(ADC1) == 0)
     {
   #if (USE_TIMEOUT == 1)
   /* Check Systick counter flag to decrement the time-out value */
   if (LL_SYSTICK_IsActiveCounterFlag())
     {
   if(Timeout-- == 0)
         {
   Error_Handler();
         }
     }
   #endif /* USE_TIMEOUT */
     }
   /* Clear flag ADC channel configuration ready */
   LL_ADC_ClearFlag_CCRDY(ADC1);
  LL_ADC_SetChannelSamplingTime(ADC1, LL_ADC_CHANNEL_VREFINT, LL_ADC_SAMPLINGTIME_COMMON_2);
  /* USER CODE BEGIN ADC1_Init 2 */
    LL_DMA_SetPeriphAddress(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL, LL_ADC_DMA_GetRegAddr(PB_ADC_INSTANCE, LL_ADC_DMA_REG_REGULAR_DATA));
    LL_DMA_EnableIT_TC(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL);
    LL_DMA_EnableIT_TE(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL);
  /* USER CODE END ADC1_Init 2 */

}

/* USER CODE BEGIN 1 */
#define ADC_CONTROL_TIMEOUT_MS 2u
#define ADC_CALIBRATION_TIMEOUT_MS 3u
#define ADC_CLOCK_HZ 16000000u

static volatile ADC_ScanStatus s_scan_state;

static void adc_wait_cycles(uint32_t cycles) {
    while (cycles--) __NOP();
}

static uint8_t adc_enable(void) {
    LL_ADC_ClearFlag_ADRDY(PB_ADC_INSTANCE);
    LL_ADC_Enable(PB_ADC_INSTANCE);
    uint32_t start = Millis();
    while (!LL_ADC_IsActiveFlag_ADRDY(PB_ADC_INSTANCE)) {
        if ((uint32_t)(Millis() - start) > ADC_CONTROL_TIMEOUT_MS)
            return (uint8_t)LL_ADC_IsActiveFlag_ADRDY(PB_ADC_INSTANCE);
    }
    return 1u;
}

static void adc_stop_dma(void) {
    LL_DMA_DisableChannel(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL);
    WRITE_REG(PB_ADC_DMA_INSTANCE->IFCR, PB_DMA_CHANNEL_FLAGS(PB_ADC_DMA_CHANNEL, DMA_IFCR_CGIF1));
}

uint8_t ADC_ScanStop(void) {
    s_scan_state = ADC_SCAN_IDLE;
    if (LL_ADC_REG_IsConversionOngoing(PB_ADC_INSTANCE) && !LL_ADC_REG_IsStopConversionOngoing(PB_ADC_INSTANCE))
        LL_ADC_REG_StopConversion(PB_ADC_INSTANCE);
    uint32_t start = Millis();
    while (READ_BIT(PB_ADC_INSTANCE->CR, ADC_CR_ADSTART | ADC_CR_ADSTP)) {
        if ((uint32_t)(Millis() - start) > ADC_CONTROL_TIMEOUT_MS) {
            if (!READ_BIT(PB_ADC_INSTANCE->CR, ADC_CR_ADSTART | ADC_CR_ADSTP)) break;
            adc_stop_dma();
            return 0u;
        }
    }
    adc_stop_dma();

    if (LL_ADC_IsEnabled(PB_ADC_INSTANCE) && !LL_ADC_IsDisableOngoing(PB_ADC_INSTANCE)) LL_ADC_Disable(PB_ADC_INSTANCE);
    start = Millis();
    while (LL_ADC_IsEnabled(PB_ADC_INSTANCE)) {
        if ((uint32_t)(Millis() - start) > ADC_CONTROL_TIMEOUT_MS && LL_ADC_IsEnabled(PB_ADC_INSTANCE)) return 0u;
    }
    LL_ADC_REG_SetDMATransfer(PB_ADC_INSTANCE, LL_ADC_REG_DMA_TRANSFER_NONE);
    WRITE_REG(PB_ADC_INSTANCE->ISR, ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
    return 1u;
}

uint8_t ADC_Calibrate(void) {
    if (!ADC_ScanStop()) return 0u;

    /* Preserve the G0 HAL's eight-sample calibration and +1 correction. */
    uint32_t sum = 0u;
    uint32_t start = Millis();
    for (uint8_t i = 0u; i < 8u; ++i) {
        LL_ADC_StartCalibration(PB_ADC_INSTANCE);
        while (LL_ADC_IsCalibrationOnGoing(PB_ADC_INSTANCE)) {
            if ((uint32_t)(Millis() - start) > ADC_CALIBRATION_TIMEOUT_MS &&
                LL_ADC_IsCalibrationOnGoing(PB_ADC_INSTANCE)) return 0u;
        }
        sum += LL_ADC_GetCalibrationFactor(PB_ADC_INSTANCE) + 1u;
    }
    adc_wait_cycles(LL_ADC_DELAY_CALIB_ENABLE_ADC_CYCLES *
                     ((SystemCoreClock + ADC_CLOCK_HZ - 1u) / ADC_CLOCK_HZ));
    if (!adc_enable()) return 0u;
    LL_ADC_SetCalibrationFactor(PB_ADC_INSTANCE, (sum + 4u) / 8u);
    return ADC_ScanStop();
}

uint8_t ADC_ScanStart(uint16_t samples[PB_ADC_SCAN_COUNT]) {
    if (s_scan_state == ADC_SCAN_BUSY || READ_BIT(PB_ADC_INSTANCE->CR, ADC_CR_ADSTART | ADC_CR_ADSTP | ADC_CR_ADDIS))
        return 0u;
    if (!LL_ADC_IsEnabled(PB_ADC_INSTANCE)) {
        /* The PM platform restores TSEN/VREFEN before the next scan. */
        adc_wait_cycles(LL_ADC_DELAY_TEMPSENSOR_STAB_US * (SystemCoreClock / 1000000u));
        LL_ADC_REG_SetDMATransfer(PB_ADC_INSTANCE, LL_ADC_REG_DMA_TRANSFER_LIMITED);
        if (!adc_enable()) return 0u;
    }

    WRITE_REG(PB_ADC_INSTANCE->ISR, ADC_ISR_EOC | ADC_ISR_EOS | ADC_ISR_OVR);
    WRITE_REG(PB_ADC_DMA_INSTANCE->IFCR, PB_DMA_CHANNEL_FLAGS(PB_ADC_DMA_CHANNEL, DMA_IFCR_CGIF1));
    LL_DMA_SetMemoryAddress(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL, (uint32_t)(uintptr_t)samples);
    LL_DMA_SetDataLength(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL, PB_ADC_SCAN_COUNT);
    s_scan_state = ADC_SCAN_BUSY;
    LL_DMA_EnableChannel(PB_ADC_DMA_INSTANCE, PB_ADC_DMA_CHANNEL);
    LL_ADC_REG_StartConversion(PB_ADC_INSTANCE);
    return 1u;
}

ADC_ScanStatus ADC_ScanState(void) {
    ADC_ScanStatus state = s_scan_state;
    if (state == ADC_SCAN_DONE) __DMB();
    return state == ADC_SCAN_BUSY && LL_ADC_IsActiveFlag_OVR(PB_ADC_INSTANCE) ? ADC_SCAN_ERROR : state;
}

void ADC_DmaIRQ(void) {
    uint32_t flags = READ_REG(PB_ADC_DMA_INSTANCE->ISR) & (PB_DMA_CHANNEL_FLAGS(PB_ADC_DMA_CHANNEL, DMA_ISR_TCIF1) | PB_DMA_CHANNEL_FLAGS(PB_ADC_DMA_CHANNEL, DMA_ISR_TEIF1));
    if (!flags) return;
    adc_stop_dma();
    if (s_scan_state == ADC_SCAN_BUSY) {
        s_scan_state = (flags & PB_DMA_CHANNEL_FLAGS(PB_ADC_DMA_CHANNEL, DMA_ISR_TEIF1)) || LL_ADC_IsActiveFlag_OVR(PB_ADC_INSTANCE)
                           ? ADC_SCAN_ERROR : ADC_SCAN_DONE;
    }
}
/* USER CODE END 1 */

