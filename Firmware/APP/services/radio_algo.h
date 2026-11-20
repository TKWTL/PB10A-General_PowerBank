#ifndef PB10A_RADIO_ALGO_H
#define PB10A_RADIO_ALGO_H
#include <stdint.h>
/* Asynchronous RDA5807 queue: one HAL transfer at a time. */
#define RADIO_IO_PENDING 2u
uint8_t RadioAlgo_QueueWrite(uint8_t reg,uint16_t value);
uint8_t RadioAlgo_TakeStatus(uint16_t *status,uint16_t *signal);
void RadioAlgo_Service(void);
void RadioAlgo_ResetStatus(void);
uint8_t RadioAlgo_IOBusy(void);
uint8_t RadioAlgo_TakeIOError(void);
/* Register RDA5807 low-power behavior and force startup power-down. */
void RadioAlgo_Init(void);
/* 1 if the boot RDA5807 register power-down I2C write was acknowledged. */
uint8_t RadioAlgo_BootSleepAcknowledged(void);
#endif
