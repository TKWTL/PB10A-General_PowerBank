/* BMP180 API adapted from TKWTL/Collar-Radio/Drivers/BMP180. */
#ifndef PB10A_BMP180_H
#define PB10A_BMP180_H
#include <stdint.h>

/* Optional legacy outputs. PB10A uses the integer compensation API. */
#ifndef BMP180_USE_FLOAT_API
#define BMP180_USE_FLOAT_API 1
#endif

#define BMP180_ADDRESS          0xEEu /* 7-bit 0x77 shifted for HAL */
#define BMP180_ADCVALUE_BASE    0xF6u
#define BMP180_CONTROL          0xF4u
#define BMP180_SOFTRESET        0xE0u
#define BMP180_ID               0xD0u
#define BMP180_CALVALUE_BASE    0xAAu
#define BMP180_CMD_TEMP         0x2Eu
#define BMP180_CMD_PRESS_OSS3   0xF4u
extern int32_t BMP180_AirPressure; /* Pa */
#if BMP180_USE_FLOAT_API
extern float BMP180_AirTemprature; /* reference naming retained */
extern float BMP180_AirPressure_kPa;
extern float BMP180_Altitude;
#endif
uint8_t BMP180_IsInitialized(void);
void BMP180_Invalidate(void);
void BMP180_Init(void);
void BMP180_StartConv(uint8_t code);
void BMP180_GetTemprature(void); /* requires 5 ms after start */
void BMP180_GetPressure(void);  /* requires 26 ms after OSS3 start */
#if BMP180_USE_FLOAT_API
void BMP180_CalcAltitude(void);
#endif
/* Atomic multi-step sample, holds one I2C bus lock through both conversions.
 * Synchronous, blocking: not for IRQ or a running PM STOP transition. */
uint8_t BMP180_ReadSample(void);

/* Async APP service uses these conversion-only BSP functions. */
/* Caller owns pt, data and flag until completion. */
struct pt;
char BMP180_TransferPT(struct pt *pt, uint8_t read, uint8_t reg, uint8_t *data,
                       uint16_t count, volatile uint8_t *flag);
uint8_t BMP180_AsyncRead(uint8_t reg,uint8_t *data,uint16_t count,uint8_t *flag);
uint8_t BMP180_AsyncWrite(uint8_t reg,uint8_t *data,uint16_t count,uint8_t *flag);
uint8_t BMP180_LoadCalibration(const uint8_t *data);
uint8_t BMP180_ApplyTemperature(const uint8_t *data,int *temp10);
uint8_t BMP180_ApplyPressure(const uint8_t *data,int *pressure_hpa10);

#endif
