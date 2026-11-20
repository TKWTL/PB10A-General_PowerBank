/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
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
#include "i2c.h"

/* USER CODE BEGIN 0 */
#include "millis.h"
/* USER CODE END 0 */

I2C_HandleTypeDef hi2c1;

/* I2C1 init function */
void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x10B17DB5;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspInit 0 */

  /* USER CODE END I2C1_MspInit 0 */
    LL_RCC_SetI2CClockSource(LL_RCC_I2C1_CLKSOURCE_PCLK1);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**I2C1 GPIO Configuration
    PB9     ------> I2C1_SDA
    PB8     ------> I2C1_SCL
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9|GPIO_PIN_8;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_MEDIUM;
    GPIO_InitStruct.Alternate = GPIO_AF6_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* I2C1 clock enable */
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* I2C1 interrupt Init */
    HAL_NVIC_SetPriority(I2C1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(I2C1_IRQn);
  /* USER CODE BEGIN I2C1_MspInit 1 */

  /* USER CODE END I2C1_MspInit 1 */
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C1)
  {
  /* USER CODE BEGIN I2C1_MspDeInit 0 */

  /* USER CODE END I2C1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C1_CLK_DISABLE();

    /**I2C1 GPIO Configuration
    PB9     ------> I2C1_SDA
    PB8     ------> I2C1_SCL
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_9);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_8);

    /* I2C1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(I2C1_IRQn);
  /* USER CODE BEGIN I2C1_MspDeInit 1 */

  /* USER CODE END I2C1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/***************************** I2C1 事务仲裁 *****************************
 * STM32 HAL interrupt callbacks own the hardware transfer state machine.
 * This layer handles a single in-flight transaction, async completion,
 * bounded abort, and synchronous compatibility for unmigrated sensors.
 * No software I2C START/ACK state machine is duplicated here.
 */
#define PB_I2C_TIMEOUT_MS 30u
#define PB_I2C_ABORT_TIMEOUT_MS 10u
enum {I2C_IDLE=0u, I2C_BUSY, I2C_DONE, I2C_FAILED, I2C_ABORTING, I2C_POISONED};
static volatile uint8_t s_state;
static uint8_t s_lock_depth;
static uint8_t s_probe_dummy;
static uint16_t s_address;
static uint32_t s_start_ms;
static volatile uint8_t *s_async_flag;

uint8_t I2C_BusIsLocked(void) {
    return (uint8_t)(s_lock_depth!=0u || s_state==I2C_BUSY || s_state==I2C_ABORTING || s_state==I2C_POISONED);
}

uint8_t I2C_TransactionBegin(void) {
    if(s_state==I2C_BUSY || s_state==I2C_ABORTING || s_state==I2C_POISONED || s_lock_depth==255u) return I2C_ERROR;
    ++s_lock_depth;
    return I2C_OK;
}

void I2C_TransactionEnd(void) {
    if(s_lock_depth) --s_lock_depth;
}

static void i2c_finish(uint8_t success) {
    if(s_async_flag) {
        *s_async_flag = success ? I2C_ASYNC_DONE : I2C_ASYNC_ERROR;
        s_async_flag = 0;
        s_state = I2C_IDLE;
    }
    else s_state = success ? I2C_DONE : I2C_FAILED;
}

void I2C_AsyncService(void) {
    if(s_state==I2C_BUSY && (uint32_t)(Millis()-s_start_ms)>=PB_I2C_TIMEOUT_MS) {
        s_state=I2C_ABORTING;
        s_start_ms=Millis();
        if(HAL_I2C_Master_Abort_IT(&hi2c1,s_address)!=HAL_OK) {
            if(s_async_flag) {*s_async_flag=I2C_ASYNC_ERROR;s_async_flag=0;}
            s_state=I2C_POISONED;
        }
    }
    else if(s_state==I2C_ABORTING && (uint32_t)(Millis()-s_start_ms)>=PB_I2C_ABORT_TIMEOUT_MS) {
        if(s_async_flag) {*s_async_flag=I2C_ASYNC_ERROR;s_async_flag=0;}
        s_state=I2C_POISONED; /* do not reuse HAL handle before explicit recovery */
    }
}

static HAL_StatusTypeDef i2c_hal_start(uint8_t op,uint16_t addr,uint8_t reg,uint8_t *p,uint16_t n) {
    switch(op) {
    case 0u:return HAL_I2C_Mem_Write_IT(&hi2c1,addr,reg,I2C_MEMADD_SIZE_8BIT,p,n);
    case 1u:return HAL_I2C_Mem_Read_IT(&hi2c1,addr,reg,I2C_MEMADD_SIZE_8BIT,p,n);
    case 2u:return HAL_I2C_Master_Transmit_IT(&hi2c1,addr,p,n);
    case 3u:return HAL_I2C_Master_Receive_IT(&hi2c1,addr,p,n);
    default:return HAL_ERROR;
    }
}

static uint8_t i2c_submit(uint8_t op,uint16_t addr,uint8_t reg,uint8_t *data,uint16_t n,volatile uint8_t *flag) {
    HAL_StatusTypeDef result;
    if(s_state!=I2C_IDLE || s_lock_depth || HAL_I2C_GetState(&hi2c1)!=HAL_I2C_STATE_READY) return 0u;
    if(!data || !flag) return 0u;
    *flag=I2C_ASYNC_PENDING;
    s_async_flag=flag;
    s_address=addr;
    s_start_ms=Millis();
    s_state=I2C_BUSY;
    result=i2c_hal_start(op,addr,reg,data,n);
    if(result!=HAL_OK) {
        s_async_flag=0;
        *flag=I2C_ASYNC_ERROR;
        s_state=I2C_IDLE;
    }
    return 1u; /* accepted or immediate HAL failure; completion flag is authoritative */
}

uint8_t ASYNC_I2C_Transmit(uint16_t a,uint8_t reg,uint8_t *data,uint16_t n,uint8_t priority,uint8_t *flag) {
    (void)priority;
    if(!n)return 0u;
    return i2c_submit(0u,a,reg,data,n,flag);
}

uint8_t ASYNC_I2C_Receive(uint16_t a,uint8_t reg,uint8_t *data,uint16_t n,uint8_t priority,uint8_t *flag) {
    (void)priority;
    if(!n)return 0u;
    return i2c_submit(1u,a,reg,data,n,flag);
}

uint8_t I2C_AsyncProbe(uint16_t a,uint8_t *flag) {
    return i2c_submit(2u,a,0u,&s_probe_dummy,0u,flag);
}

/************************** 同步兼容接口 **************************
 * The existing sensor drivers still wait inside their own call stack.
 * Nonblocking SW6306 and scanner callers must use the ASYNC API.
 */
static uint8_t i2c_sync(uint8_t op,uint16_t a,uint8_t reg,uint8_t *data,uint16_t n) {
    HAL_StatusTypeDef start;
    uint32_t start_ms;
    uint8_t result;
    if(s_state!=I2C_IDLE || HAL_I2C_GetState(&hi2c1)!=HAL_I2C_STATE_READY || !data) return I2C_ERROR;
    s_async_flag=0;
    s_state=I2C_BUSY;
    s_address=a;
    s_start_ms=Millis();
    start=i2c_hal_start(op,a,reg,data,n);
    if(start!=HAL_OK) {s_state=I2C_IDLE;return I2C_ERROR;}
    start_ms=Millis();
    while(s_state==I2C_BUSY || s_state==I2C_ABORTING) {
        I2C_AsyncService();
        if((uint32_t)(Millis()-start_ms)>(PB_I2C_TIMEOUT_MS+PB_I2C_ABORT_TIMEOUT_MS+1u)) {
            s_state=I2C_POISONED; /* ISR or clock failure; never hang forever */
            return I2C_ERROR;
        }
    }
    result=(uint8_t)(s_state==I2C_DONE?I2C_OK:I2C_ERROR);
    if(s_state!=I2C_POISONED)s_state=I2C_IDLE;
    return result;
}

uint8_t I2C_RegWrite(uint16_t a,uint8_t reg,const uint8_t *p,uint16_t n) {
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_sync(0u,a,reg,(uint8_t *)(void *)p,n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_RegRead(uint16_t a,uint8_t reg,uint8_t *p,uint16_t n) {
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_sync(1u,a,reg,p,n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_Transmit(uint16_t a,const uint8_t *p,uint16_t n) {
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_sync(2u,a,0u,(uint8_t *)(void *)p,n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_Receive(uint16_t a,uint8_t *p,uint16_t n) {
    uint8_t result;
    if(!p || !n || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    result=i2c_sync(3u,a,0u,p,n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_IsReady(uint16_t address) {
    uint8_t result;
    if(I2C_TransactionBegin()!=I2C_OK)return 0u;
    result=i2c_sync(2u,address,0u,&s_probe_dummy,0u);
    I2C_TransactionEnd();
    return (uint8_t)(result==I2C_OK);
}

/*************************** HAL IRQ 回调 ***************************/
void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && s_state==I2C_BUSY)i2c_finish(1u);
}
void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && s_state==I2C_BUSY)i2c_finish(1u);
}
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && s_state==I2C_BUSY)i2c_finish(1u);
}
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && s_state==I2C_BUSY)i2c_finish(1u);
}
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && (s_state==I2C_BUSY || s_state==I2C_ABORTING))i2c_finish(0u);
}
void HAL_I2C_AbortCpltCallback(I2C_HandleTypeDef *h) {
    if(h->Instance==I2C1 && s_state==I2C_ABORTING)i2c_finish(0u);
}
/* USER CODE END 1 */

