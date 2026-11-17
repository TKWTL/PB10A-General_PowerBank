#ifndef PB10A_BSP_I2C_H
#define PB10A_BSP_I2C_H
#include <stdint.h>
/* Addresses are the STM32 HAL 8-bit (7-bit << 1) form.
 * The bus is cooperative and single-owner until an async layer is added. */
#define I2C_OK 0u
#define I2C_ERROR 1u
uint8_t I2C_RegWrite(uint16_t address,uint8_t register_addr,const uint8_t *data,uint16_t count);
uint8_t I2C_RegRead(uint16_t address,uint8_t register_addr,uint8_t *data,uint16_t count);
uint8_t I2C_Transmit(uint16_t address,const uint8_t *data,uint16_t count);
uint8_t I2C_Receive(uint16_t address,uint8_t *data,uint16_t count);
uint8_t I2C_IsReady(uint16_t address);
#endif
