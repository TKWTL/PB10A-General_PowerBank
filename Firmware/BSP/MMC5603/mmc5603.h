/* MEMSIC MMC5603NJ (Rev. B), basic register/ID/low-power interface. */
#ifndef PB10A_MMC5603_H
#define PB10A_MMC5603_H
#include <stdint.h>
#define MMC5603_I2C_ADDR        0x60u /* 7-bit 0x30, HAL format */
#define MMC5603_REG_XOUT0       0x00u
#define MMC5603_REG_XOUT1       0x01u
#define MMC5603_REG_YOUT0       0x02u
#define MMC5603_REG_YOUT1       0x03u
#define MMC5603_REG_ZOUT0       0x04u
#define MMC5603_REG_ZOUT1       0x05u
#define MMC5603_REG_XOUT2       0x06u
#define MMC5603_REG_YOUT2       0x07u
#define MMC5603_REG_ZOUT2       0x08u
#define MMC5603_REG_TOUT        0x09u
#define MMC5603_REG_STATUS1     0x18u
#define MMC5603_REG_ODR         0x1Au
#define MMC5603_REG_CTRL0       0x1Bu
#define MMC5603_REG_CTRL1       0x1Cu
#define MMC5603_REG_CTRL2       0x1Du
#define MMC5603_REG_ST_X_TH     0x1Eu
#define MMC5603_REG_ST_Y_TH     0x1Fu
#define MMC5603_REG_ST_Z_TH     0x20u
#define MMC5603_REG_ST_X        0x27u
#define MMC5603_REG_ST_Y        0x28u
#define MMC5603_REG_ST_Z        0x29u
#define MMC5603_REG_PRODUCT_ID  0x39u
#define MMC5603_PRODUCT_ID      0x10u
#define MMC5603_CTRL0_TM_M      0x01u
#define MMC5603_CTRL0_TM_T      0x02u
#define MMC5603_CTRL0_AUTO_SR   0x20u
#define MMC5603_CTRL2_CMM_EN   0x10u
#define MMC5603_STATUS_M_DONE   0x40u
uint8_t MMC5603_ReadReg(uint8_t reg,uint8_t *value);
uint8_t MMC5603_WriteReg(uint8_t reg,uint8_t value);
uint8_t MMC5603_ReadID(uint8_t *id);
uint8_t MMC5603_Probe(void);
uint8_t MMC5603_EnterLowPower(void);
uint8_t MMC5603_StartSingleMeasurement(void);
#endif
