#ifndef PB10A_BSP_I2C_H
#define PB10A_BSP_I2C_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
    /* One STM32 HAL I2C1 handle and IRQ; addresses are 7-bit address << 1.
    * All API calls are foreground-only. Never call from an ISR.
    * The entire multi-register operation owns the transaction lock; _IT calls
    * are completion-waited by the synchronous legacy API adapter.
    *
    * NOTE: the foreground wait suspends all other cooperative coroOS threads,
    * though hardware I2C activity itself is IRQ driven. A real coroutine yield
    * requires a separate future nonblocking driver API.
    */
#define I2C_OK    0u
#define I2C_ERROR 1u
    uint8_t I2C_TransactionBegin(void);
    void I2C_TransactionEnd(void);
    uint8_t I2C_BusIsLocked(void);
    uint8_t I2C_RegWrite(uint16_t address,uint8_t register_addr,const uint8_t *data,uint16_t count);
    uint8_t I2C_RegRead(uint16_t address,uint8_t register_addr,uint8_t *data,uint16_t count);
    uint8_t I2C_Transmit(uint16_t address,const uint8_t *data,uint16_t count);
    uint8_t I2C_Receive(uint16_t address,uint8_t *data,uint16_t count);
    uint8_t I2C_IsReady(uint16_t address);
#ifdef __cplusplus
}
#endif
#endif
