/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    spi.c
  * @brief   This file provides code for the configuration
  *          of the SPI instances.
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
#include "spi.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* SPI1 init function */
void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  LL_SPI_InitTypeDef SPI_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* Peripheral clock enable */
  LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOA);
  /**SPI1 GPIO Configuration
  PA1   ------> SPI1_SCK
  PA2   ------> SPI1_MOSI
  PA4   ------> SPI1_NSS
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_1;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_2;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_HIGH;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_4;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_NO;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_0;
  LL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* SPI1 DMA Init */

  /* SPI1_TX Init */
  LL_DMA_SetPeriphRequest(DMA1, LL_DMA_CHANNEL_1, LL_DMAMUX_REQ_SPI1_TX);

  LL_DMA_SetDataTransferDirection(DMA1, LL_DMA_CHANNEL_1, LL_DMA_DIRECTION_MEMORY_TO_PERIPH);

  LL_DMA_SetChannelPriorityLevel(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PRIORITY_LOW);

  LL_DMA_SetMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MODE_NORMAL);

  LL_DMA_SetPeriphIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PERIPH_NOINCREMENT);

  LL_DMA_SetMemoryIncMode(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MEMORY_INCREMENT);

  LL_DMA_SetPeriphSize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_PDATAALIGN_BYTE);

  LL_DMA_SetMemorySize(DMA1, LL_DMA_CHANNEL_1, LL_DMA_MDATAALIGN_BYTE);

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  SPI_InitStruct.TransferDirection = LL_SPI_FULL_DUPLEX;
  SPI_InitStruct.Mode = LL_SPI_MODE_MASTER;
  SPI_InitStruct.DataWidth = LL_SPI_DATAWIDTH_8BIT;
  SPI_InitStruct.ClockPolarity = LL_SPI_POLARITY_LOW;
  SPI_InitStruct.ClockPhase = LL_SPI_PHASE_1EDGE;
  SPI_InitStruct.NSS = LL_SPI_NSS_HARD_OUTPUT;
  SPI_InitStruct.BaudRate = LL_SPI_BAUDRATEPRESCALER_DIV4;
  SPI_InitStruct.BitOrder = LL_SPI_MSB_FIRST;
  SPI_InitStruct.CRCCalculation = LL_SPI_CRCCALCULATION_DISABLE;
  SPI_InitStruct.CRCPoly = 7;
  LL_SPI_Init(SPI1, &SPI_InitStruct);
  LL_SPI_SetStandard(SPI1, LL_SPI_PROTOCOL_MOTOROLA);
  LL_SPI_EnableNSSPulseMgt(SPI1);
  /* USER CODE BEGIN SPI1_Init 2 */
  /* Keep the OLED SPI clock at 8 MHz and prevent per-byte NSS pulses.
   * .ioc is updated to 8 MHz; guard against CubeMX default NSSP setting. */
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
  if(HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
  /* USER CODE END SPI1_Init 2 */

}

/* USER CODE BEGIN 1 */
/* SPI1 DMA TX transport. Only this CubeMX user region invokes
 * HAL_SPI_* transfers. Caller owns the DMA source until DONE/ERROR.
 * No blocking HAL_Delay, HAL_SPI_Transmit, or HAL_SPI_Abort.
 */
static volatile SPI1_DmaState s_spi1_tx_state=SPI1_DMA_IDLE;
uint8_t SPI1_DmaTxStart(const uint8_t *bytes,uint16_t count)
{
    if(bytes==0 || count==0u || s_spi1_tx_state!=SPI1_DMA_IDLE ||
       HAL_SPI_GetState(&hspi1)!=HAL_SPI_STATE_READY)
        return 0u;
    s_spi1_tx_state=SPI1_DMA_BUSY;
    if(HAL_SPI_Transmit_DMA(&hspi1,(uint8_t *)(void *)bytes,count)!=HAL_OK)
    {
        s_spi1_tx_state=SPI1_DMA_ERROR;
        return 0u;
    }
    return 1u;
}
SPI1_DmaState SPI1_DmaTxState(void)
{
    return s_spi1_tx_state;
}
uint8_t SPI1_DmaTxRelease(void)
{
    if(s_spi1_tx_state!=SPI1_DMA_DONE)return 0u;
    s_spi1_tx_state=SPI1_DMA_IDLE;
    return 1u;
}
uint8_t SPI1_DmaTxAbortAsync(void)
{
    if(s_spi1_tx_state!=SPI1_DMA_BUSY &&
       s_spi1_tx_state!=SPI1_DMA_ERROR)return 0u;
    s_spi1_tx_state=SPI1_DMA_ABORTING;
    if(HAL_SPI_Abort_IT(&hspi1)!=HAL_OK)
    {
        s_spi1_tx_state=SPI1_DMA_ERROR;
        return 0u;
    }
    return 1u;
}
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_BUSY)
        s_spi1_tx_state=SPI1_DMA_DONE;
}
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_BUSY)
        s_spi1_tx_state=SPI1_DMA_ERROR;
}
void HAL_SPI_AbortCpltCallback(SPI_HandleTypeDef *hspi)
{
    if(hspi->Instance==SPI1 && s_spi1_tx_state==SPI1_DMA_ABORTING)
        s_spi1_tx_state=SPI1_DMA_IDLE;
}
/* USER CODE END 1 */

