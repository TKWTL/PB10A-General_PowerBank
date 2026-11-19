#ifndef PB10A_RADIO_ALGO_H
#define PB10A_RADIO_ALGO_H
#include <stdint.h>
/* Register RDA5807 low-power behavior and force startup power-down. */
void RadioAlgo_Init(void);
/* 1 if the boot RDA5807 register power-down I2C write was acknowledged. */
uint8_t RadioAlgo_BootSleepAcknowledged(void);
#endif
