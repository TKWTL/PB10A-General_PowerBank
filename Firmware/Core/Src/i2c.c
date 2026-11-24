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
#include "pt.h"
/* USER CODE END 0 */

/* I2C1 init function */
void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  LL_I2C_InitTypeDef I2C_InitStruct = {0};

  LL_GPIO_InitTypeDef GPIO_InitStruct = {0};

  LL_RCC_SetI2CClockSource(LL_RCC_I2C1_CLKSOURCE_PCLK1);

  LL_IOP_GRP1_EnableClock(LL_IOP_GRP1_PERIPH_GPIOB);
  /**I2C1 GPIO Configuration
  PB9   ------> I2C1_SDA
  PB8   ------> I2C1_SCL
  */
  GPIO_InitStruct.Pin = LL_GPIO_PIN_9;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_6;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LL_GPIO_PIN_8;
  GPIO_InitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
  GPIO_InitStruct.Speed = LL_GPIO_SPEED_FREQ_MEDIUM;
  GPIO_InitStruct.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  GPIO_InitStruct.Pull = LL_GPIO_PULL_UP;
  GPIO_InitStruct.Alternate = LL_GPIO_AF_6;
  LL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* Peripheral clock enable */
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);

  /* I2C1 interrupt Init */
  NVIC_SetPriority(I2C1_IRQn, 0);
  NVIC_EnableIRQ(I2C1_IRQn);

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */

  /** I2C Initialization
  */
  I2C_InitStruct.PeripheralMode = LL_I2C_MODE_I2C;
  I2C_InitStruct.Timing = 0x10B17DB5;
  I2C_InitStruct.AnalogFilter = LL_I2C_ANALOGFILTER_ENABLE;
  I2C_InitStruct.DigitalFilter = 0;
  I2C_InitStruct.OwnAddress1 = 0;
  I2C_InitStruct.TypeAcknowledge = LL_I2C_ACK;
  I2C_InitStruct.OwnAddrSize = LL_I2C_OWNADDRESS1_7BIT;
  LL_I2C_Init(I2C1, &I2C_InitStruct);
  LL_I2C_EnableAutoEndMode(I2C1);
  LL_I2C_SetOwnAddress2(I2C1, 0, LL_I2C_OWNADDRESS2_NOMASK);
  LL_I2C_DisableOwnAddress2(I2C1);
  LL_I2C_DisableGeneralCall(I2C1);
  LL_I2C_EnableClockStretching(I2C1);
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/* USER CODE BEGIN 1 */
#define PB_I2C_TIMEOUT_MS 30u
#define PB_I2C_ABORT_TIMEOUT_MS 10u
#define PB_I2C_DATA_IRQS (I2C_CR1_TXIE | I2C_CR1_RXIE | I2C_CR1_TCIE)
#define PB_I2C_END_IRQS (I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE)

typedef enum {
    I2C_MEM_WRITE,
    I2C_MEM_READ,
    I2C_MASTER_TX,
    I2C_MASTER_RX,
    I2C_PROBE
} i2c_op_t;

enum { I2C_IDLE, I2C_BUSY, I2C_DONE, I2C_FAILED, I2C_ABORTING, I2C_POISONED };
static volatile uint8_t s_state;
static uint8_t s_lock_depth;
static volatile uint32_t s_start_ms;
static volatile uint8_t *s_async_flag;
static uint8_t *s_data;
static uint16_t s_remaining;
static uint16_t s_address;
static uint8_t s_register;
static uint8_t s_send_register;
static uint8_t s_restart_read;
static uint8_t s_failed;

uint8_t I2C_BusIsLocked(void) {
    return (uint8_t)(s_lock_depth != 0u || s_state == I2C_BUSY || s_state == I2C_ABORTING ||
                     s_state == I2C_POISONED || LL_I2C_IsActiveFlag_BUSY(I2C1));
}

uint8_t I2C_TransactionBegin(void) {
    if (s_state != I2C_IDLE || s_lock_depth == 255u) return I2C_ERROR;
    ++s_lock_depth;
    return I2C_OK;
}

void I2C_TransactionEnd(void) {
    if (s_lock_depth) --s_lock_depth;
}

/* Shared by IRQ completion and the timeout service with interrupts masked. */
static void i2c_finish(uint8_t success, uint8_t poisoned) {
    CLEAR_BIT(I2C1->CR1, PB_I2C_DATA_IRQS | PB_I2C_END_IRQS);
    if (!poisoned) {
        WRITE_REG(I2C1->CR2, 0u);
        if (READ_BIT(I2C1->ISR, I2C_ISR_RXNE)) (void)LL_I2C_ReceiveData8(I2C1);
        LL_I2C_ClearFlag_TXE(I2C1);
    }
    if (s_async_flag) {
        *s_async_flag = success ? I2C_ASYNC_DONE : I2C_ASYNC_ERROR;
        s_async_flag = 0;
        s_state = poisoned ? I2C_POISONED : I2C_IDLE;
    } else {
        s_state = poisoned ? I2C_POISONED : (success ? I2C_DONE : I2C_FAILED);
    }
}

static void i2c_abort(uint32_t now) {
    CLEAR_BIT(I2C1->CR1, PB_I2C_DATA_IRQS);
    s_start_ms = now;
    s_state = I2C_ABORTING;
    /* Match the HAL master abort: leave reload mode and request STOP. */
    LL_I2C_HandleTransfer(I2C1, s_address, LL_I2C_ADDRESSING_MODE_7BIT, 1u,
                          LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_STOP);
}

static void i2c_load_chunk(uint32_t request) {
    /* The register prefix shares the first write chunk with its payload. */
    uint32_t count = (uint32_t)s_remaining + s_send_register;
    uint32_t end_mode = LL_I2C_MODE_AUTOEND;
    if (count > 255u) {
        count = 255u;
        end_mode = LL_I2C_MODE_RELOAD;
    }
    LL_I2C_HandleTransfer(I2C1, s_address, LL_I2C_ADDRESSING_MODE_7BIT, count, end_mode, request);
}

static void i2c_start(i2c_op_t op, uint16_t address, uint8_t reg, uint8_t *data, uint16_t count,
                      volatile uint8_t *flag) {
    uint32_t data_irqs = I2C_CR1_TCIE;
    if (op != I2C_PROBE) data_irqs |= op == I2C_MASTER_RX ? I2C_CR1_RXIE : I2C_CR1_TXIE;

    s_data = data;
    s_remaining = count;
    s_address = address;
    s_register = reg;
    s_send_register = (uint8_t)(op == I2C_MEM_WRITE || op == I2C_MEM_READ);
    s_restart_read = (uint8_t)(op == I2C_MEM_READ);
    s_failed = 0u;
    s_async_flag = flag;
    s_start_ms = Millis();
    s_state = I2C_BUSY;

    WRITE_REG(I2C1->ICR, I2C_ICR_STOPCF | I2C_ICR_NACKCF | I2C_ICR_BERRCF | I2C_ICR_ARLOCF | I2C_ICR_OVRCF);
    if (s_restart_read) {
        LL_I2C_HandleTransfer(I2C1, address, LL_I2C_ADDRESSING_MODE_7BIT, 1u,
                              LL_I2C_MODE_SOFTEND, LL_I2C_GENERATE_START_WRITE);
    } else {
        i2c_load_chunk(op == I2C_MASTER_RX ? LL_I2C_GENERATE_START_READ : LL_I2C_GENERATE_START_WRITE);
    }
    __COMPILER_BARRIER();
    SET_BIT(I2C1->CR1, data_irqs | PB_I2C_END_IRQS);
}

void I2C_TransferIRQ(void) {
    uint32_t flags = READ_REG(I2C1->ISR);
    uint32_t enabled = READ_REG(I2C1->CR1);

    if (s_state != I2C_BUSY && s_state != I2C_ABORTING) return;

    /* STM32G070 ES0468: a master-mode BERR can be spurious. */
    if (flags & I2C_ISR_BERR) LL_I2C_ClearFlag_BERR(I2C1);
    if (flags & I2C_ISR_ARLO) {
        LL_I2C_ClearFlag_ARLO(I2C1);
        /* The winning master owns the bus. */
        i2c_finish(0u, 0u);
        return;
    }
    if (flags & I2C_ISR_OVR) {
        LL_I2C_ClearFlag_OVR(I2C1);
        s_failed = 1u;
        if (s_state == I2C_BUSY && !(flags & (I2C_ISR_STOPF | I2C_ISR_NACKF))) i2c_abort(Millis());
    }
    if (flags & I2C_ISR_NACKF) {
        LL_I2C_ClearFlag_NACK(I2C1);
        CLEAR_BIT(I2C1->CR1, PB_I2C_DATA_IRQS);
        s_failed = 1u;
    }

    /* RXNE can accompany STOPF on the last byte. */
    if (s_state == I2C_BUSY && !s_failed && s_remaining &&
        (enabled & I2C_CR1_RXIE) && (flags & I2C_ISR_RXNE)) {
        *s_data++ = LL_I2C_ReceiveData8(I2C1);
        --s_remaining;
    }
    if (flags & I2C_ISR_STOPF) {
        LL_I2C_ClearFlag_STOP(I2C1);
        uint8_t complete = (uint8_t)(s_state == I2C_BUSY && !s_failed && !s_remaining &&
                                     !s_send_register && !s_restart_read);
        i2c_finish(complete, 0u);
        return;
    }
    if (s_state != I2C_BUSY || s_failed) return;

    if ((enabled & I2C_CR1_TXIE) && (flags & I2C_ISR_TXIS)) {
        if (s_send_register) {
            LL_I2C_TransmitData8(I2C1, s_register);
            s_send_register = 0u;
        } else if (!s_restart_read && s_remaining) {
            LL_I2C_TransmitData8(I2C1, *s_data++);
            --s_remaining;
        }
    }
    if (flags & I2C_ISR_TCR) {
        i2c_load_chunk(LL_I2C_GENERATE_NOSTARTSTOP);
    } else if ((flags & I2C_ISR_TC) && s_restart_read) {
        s_restart_read = 0u;
        MODIFY_REG(I2C1->CR1, I2C_CR1_TXIE, I2C_CR1_RXIE);
        i2c_load_chunk(LL_I2C_GENERATE_RESTART_7BIT_READ);
    }
}

void I2C_AsyncService(void) {
    uint8_t state = s_state;
    if (state != I2C_BUSY && state != I2C_ABORTING) return;

    uint32_t start = s_start_ms;
    uint32_t timeout = state == I2C_BUSY ? PB_I2C_TIMEOUT_MS : PB_I2C_ABORT_TIMEOUT_MS;
    if ((uint32_t)(Millis() - start) < timeout) return;

    uint32_t irq_mask = __get_PRIMASK();
    __disable_irq();
    /* Recheck after masking: the IRQ may have completed the transaction. */
    uint32_t now = Millis();
    if (s_state == I2C_BUSY && (uint32_t)(now - s_start_ms) >= PB_I2C_TIMEOUT_MS) {
        if (READ_BIT(I2C1->ISR, I2C_ISR_STOPF | I2C_ISR_ARLO)) I2C_TransferIRQ();
        if (s_state == I2C_BUSY) i2c_abort(now);
    } else if (s_state == I2C_ABORTING && (uint32_t)(now - s_start_ms) >= PB_I2C_ABORT_TIMEOUT_MS) {
        if (READ_BIT(I2C1->ISR, I2C_ISR_STOPF | I2C_ISR_ARLO)) I2C_TransferIRQ();
        if (s_state == I2C_ABORTING) i2c_finish(0u, 1u);
    }
    __set_PRIMASK(irq_mask);
}

static uint8_t i2c_submit(i2c_op_t op, uint16_t address, uint8_t reg, uint8_t *data, uint16_t count,
                          volatile uint8_t *flag) {
    if (!flag || (op != I2C_PROBE && (!data || !count))) return 0u;
    if (s_state == I2C_POISONED) {
        *flag = I2C_ASYNC_ERROR;
        return 1u;
    }
    if (s_state != I2C_IDLE || s_lock_depth) return 0u;
    if (LL_I2C_IsActiveFlag_BUSY(I2C1)) {
        *flag = I2C_ASYNC_ERROR;
        return 1u;
    }
    *flag = I2C_ASYNC_PENDING;
    i2c_start(op, address, reg, data, count, flag);
    return 1u;
}

uint8_t ASYNC_I2C_Transmit(uint16_t a, uint8_t reg, uint8_t *data, uint16_t n, uint8_t priority, uint8_t *flag) {
    (void)priority;
    return i2c_submit(I2C_MEM_WRITE, a, reg, data, n, flag);
}

uint8_t ASYNC_I2C_Receive(uint16_t a, uint8_t reg, uint8_t *data, uint16_t n, uint8_t priority, uint8_t *flag) {
    (void)priority;
    return i2c_submit(I2C_MEM_READ, a, reg, data, n, flag);
}

char I2C_RegTransferPT(struct pt *pt, uint16_t address, uint8_t read, uint8_t reg,
                       uint8_t *data, uint16_t count, volatile uint8_t *flag) {
    PT_BEGIN(pt);
    if (!flag) PT_EXIT(pt);
    if (!data || !count) {
        *flag = I2C_ASYNC_ERROR;
        PT_EXIT(pt);
    }
    PT_WAIT_UNTIL(pt, i2c_submit(read ? I2C_MEM_READ : I2C_MEM_WRITE, address, reg, data, count, flag));
    PT_WAIT_UNTIL(pt, *flag != I2C_ASYNC_PENDING);
    PT_END(pt);
}

uint8_t I2C_AsyncProbe(uint16_t a, volatile uint8_t *flag) {
    return i2c_submit(I2C_PROBE, a, 0u, 0, 0u, flag);
}

/* Blocking callers use the same IRQ engine and hold the transaction lock. */
static uint8_t i2c_sync(i2c_op_t op, uint16_t address, uint8_t reg, uint8_t *data, uint16_t count) {
    if (s_state != I2C_IDLE || LL_I2C_IsActiveFlag_BUSY(I2C1)) return I2C_ERROR;
    i2c_start(op, address, reg, data, count, 0);
    while (s_state == I2C_BUSY || s_state == I2C_ABORTING) I2C_AsyncService();
    uint8_t result = (uint8_t)(s_state == I2C_DONE ? I2C_OK : I2C_ERROR);
    if (s_state != I2C_POISONED) s_state = I2C_IDLE;
    return result;
}

uint8_t I2C_RegWrite(uint16_t a, uint8_t reg, const uint8_t *p, uint16_t n) {
    if (!p || !n || I2C_TransactionBegin() != I2C_OK) return I2C_ERROR;
    uint8_t result = i2c_sync(I2C_MEM_WRITE, a, reg, (uint8_t *)(void *)p, n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_RegRead(uint16_t a, uint8_t reg, uint8_t *p, uint16_t n) {
    if (!p || !n || I2C_TransactionBegin() != I2C_OK) return I2C_ERROR;
    uint8_t result = i2c_sync(I2C_MEM_READ, a, reg, p, n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_Transmit(uint16_t a, const uint8_t *p, uint16_t n) {
    if (!p || !n || I2C_TransactionBegin() != I2C_OK) return I2C_ERROR;
    uint8_t result = i2c_sync(I2C_MASTER_TX, a, 0u, (uint8_t *)(void *)p, n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_Receive(uint16_t a, uint8_t *p, uint16_t n) {
    if (!p || !n || I2C_TransactionBegin() != I2C_OK) return I2C_ERROR;
    uint8_t result = i2c_sync(I2C_MASTER_RX, a, 0u, p, n);
    I2C_TransactionEnd();
    return result;
}

uint8_t I2C_IsReady(uint16_t address) {
    if (I2C_TransactionBegin() != I2C_OK) return 0u;
    uint8_t result = i2c_sync(I2C_PROBE, address, 0u, 0, 0u);
    I2C_TransactionEnd();
    return (uint8_t)(result == I2C_OK);
}
/* USER CODE END 1 */

