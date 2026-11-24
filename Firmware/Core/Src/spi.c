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
#include "peripheral_map.h"
#include "millis.h"
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
    /* Keep hardware NSS asserted across OLED bytes. */
    LL_SPI_DisableNSSPulseMgt(PB_SPI_INSTANCE);
    LL_DMA_SetPeriphAddress(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL, LL_SPI_DMA_GetRegAddr(PB_SPI_INSTANCE));
    LL_DMA_EnableIT_TC(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL);
    LL_DMA_EnableIT_TE(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL);
  /* USER CODE END SPI1_Init 2 */

}

/* USER CODE BEGIN 1 */
/* The FIFO and shift register should drain well within one scheduler tick. */
#define SPI_ABORT_DRAIN_MS 2u

enum { SPI_DMA_NO_EVENT, SPI_DMA_TC, SPI_DMA_TE };
static volatile uint8_t s_dma_event;
static SPI_DmaState s_tx_state;
static uint32_t s_abort_started_ms;

static void spi_stop_dma(void) {
    LL_SPI_DisableDMAReq_TX(PB_SPI_INSTANCE);
    LL_DMA_DisableChannel(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL);
    WRITE_REG(PB_SPI_TX_DMA_INSTANCE->IFCR, PB_DMA_CHANNEL_FLAGS(PB_SPI_TX_DMA_CHANNEL, DMA_IFCR_CGIF1));
}

static uint8_t spi_wire_idle(void) {
    return (uint8_t)(LL_SPI_GetTxFIFOLevel(PB_SPI_INSTANCE) == LL_SPI_TX_FIFO_EMPTY &&
                     !LL_SPI_IsActiveFlag_BSY(PB_SPI_INSTANCE));
}

static void spi_discard_rx(void) {
    /* Full-duplex clocking still fills RX FIFO on this TX-only bus. */
    while (LL_SPI_GetRxFIFOLevel(PB_SPI_INSTANCE) != LL_SPI_RX_FIFO_EMPTY) {
        (void)LL_SPI_ReceiveData8(PB_SPI_INSTANCE);
    }
    if (LL_SPI_IsActiveFlag_OVR(PB_SPI_INSTANCE)) LL_SPI_ClearFlag_OVR(PB_SPI_INSTANCE);
}

uint8_t SPI_DmaTxStart(const uint8_t *bytes, uint16_t count) {
    if (!bytes || !count || s_tx_state != SPI_DMA_IDLE) return 0u;

    WRITE_REG(PB_SPI_TX_DMA_INSTANCE->IFCR, PB_DMA_CHANNEL_FLAGS(PB_SPI_TX_DMA_CHANNEL, DMA_IFCR_CGIF1));
    LL_DMA_SetMemoryAddress(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL, (uint32_t)(uintptr_t)bytes);
    LL_DMA_SetDataLength(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL, count);
    s_dma_event = SPI_DMA_NO_EVENT;
    s_tx_state = SPI_DMA_BUSY;

    __DMB();
    LL_DMA_EnableChannel(PB_SPI_TX_DMA_INSTANCE, PB_SPI_TX_DMA_CHANNEL);
    LL_SPI_Enable(PB_SPI_INSTANCE);
    LL_SPI_EnableDMAReq_TX(PB_SPI_INSTANCE);
    return 1u;
}

void SPI_DmaTxIRQ(void) {
    uint32_t flags = READ_REG(PB_SPI_TX_DMA_INSTANCE->ISR) & (PB_DMA_CHANNEL_FLAGS(PB_SPI_TX_DMA_CHANNEL, DMA_ISR_TCIF1) | PB_DMA_CHANNEL_FLAGS(PB_SPI_TX_DMA_CHANNEL, DMA_ISR_TEIF1));
    if (!flags) return;
    spi_stop_dma();
    s_dma_event = (flags & PB_DMA_CHANNEL_FLAGS(PB_SPI_TX_DMA_CHANNEL, DMA_ISR_TEIF1)) ? SPI_DMA_TE : SPI_DMA_TC;
}

void SPI_DmaTxService(void) {
    if (s_tx_state == SPI_DMA_BUSY) {
        uint8_t event = s_dma_event;
        if (event == SPI_DMA_TE || LL_SPI_IsActiveFlag_MODF(PB_SPI_INSTANCE)) {
            s_tx_state = SPI_DMA_ERROR;
        } else if (event == SPI_DMA_TC && spi_wire_idle()) {
            /* SPI stays enabled, so hardware NSS keeps its existing level. */
            spi_discard_rx();
            s_tx_state = SPI_DMA_DONE;
        }
    } else if (s_tx_state == SPI_DMA_ABORTING) {
        uint8_t idle = spi_wire_idle();
        if (LL_SPI_IsActiveFlag_MODF(PB_SPI_INSTANCE) ||
            (!idle && (uint32_t)(Millis() - s_abort_started_ms) >= SPI_ABORT_DRAIN_MS)) {
            /* DMA is stopped. Reset the SPI peripheral if it cannot drain normally. */
            (void)LL_SPI_DeInit(PB_SPI_INSTANCE);
            PB_SPI_INIT();
        } else if (idle) {
            spi_discard_rx();
        } else {
            return;
        }
        s_tx_state = SPI_DMA_IDLE;
    }
}

SPI_DmaState SPI_DmaTxState(void) {
    return s_tx_state;
}

uint8_t SPI_DmaTxRelease(void) {
    if (s_tx_state != SPI_DMA_DONE) return 0u;
    s_tx_state = SPI_DMA_IDLE;
    return 1u;
}

uint8_t SPI_DmaTxAbortAsync(void) {
    if (s_tx_state != SPI_DMA_BUSY && s_tx_state != SPI_DMA_ERROR) return 0u;
    spi_stop_dma();
    s_abort_started_ms = Millis();
    s_tx_state = SPI_DMA_ABORTING;
    return 1u;
}
/* USER CODE END 1 */

