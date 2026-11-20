/* BMP180 API adapted from TKWTL/Collar-Radio/Drivers/BMP180. */
#ifndef PB10A_BMP180_H
#define PB10A_BMP180_H
#include <stdint.h>
#define BMP180_ADDRESS          0xEEu /* 7-bit 0x77 shifted for HAL */
#define BMP180_ADCVALUE_BASE    0xF6u
#define BMP180_CONTROL          0xF4u
#define BMP180_SOFTRESET        0xE0u
#define BMP180_ID               0xD0u
#define BMP180_CALVALUE_BASE    0xAAu
#define BMP180_CMD_TEMP         0x2Eu
#define BMP180_CMD_PRESS_OSS3   0xF4u
extern float BMP180_AirTemprature; /* reference naming retained */
extern int32_t BMP180_AirPressure; /* Pa */
extern float BMP180_AirPressure_kPa;
extern float BMP180_Altitude;
uint8_t BMP180_IsInitialized(void);
void BMP180_Init(void);
void BMP180_StartConv(uint8_t code);
void BMP180_GetTemprature(void); /* requires 5 ms after start */
void BMP180_GetPressure(void);  /* requires 26 ms after OSS3 start */
void BMP180_CalcAltitude(void);
/* Atomic multi-step sample, holds one I2C bus lock through both conversions.
* Synchronous, blocking: not for IRQ or a running PM STOP transition. */
uint8_t BMP180_ReadSample(void);
#endif
