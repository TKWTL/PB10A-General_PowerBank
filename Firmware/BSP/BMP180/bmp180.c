/* BMP180 compensation from Collar-Radio, adapted for Cortex-M0+:
 * no unaligned pointer casts, use big-endian calibration conversion.
 * Multi-step operations hold the I2C1 bus lock for the full sequence.
 */
#include "bmp180.h"
#include "i2c.h"
#include "millis.h"
#if BMP180_USE_FLOAT_API
#include <math.h>
#endif
#define BMP180_OSS 3u
static uint8_t s_ok, s_result;
static int16_t ac1,ac2,ac3,b1,b2,mb,mc,md;
static uint16_t ac4,ac5,ac6;
static int32_t b5;
int32_t BMP180_AirPressure;
#if BMP180_USE_FLOAT_API
float BMP180_AirTemprature;
float BMP180_AirPressure_kPa;
float BMP180_Altitude;
#endif
static uint16_t be16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static int16_t s16(const uint8_t *p){return (int16_t)be16(p);}
uint8_t BMP180_IsInitialized(void){return s_ok;}
void BMP180_Invalidate(void) {
    s_ok = 0u;
}
void BMP180_Init(void) {
    uint8_t id=0u,c[22];
    s_ok=0u;
    if(I2C_TransactionBegin()!=I2C_OK)return;
    if(I2C_RegRead(BMP180_ADDRESS,BMP180_ID,&id,1u)!=I2C_OK||id!=0x55u)
        goto finish;
    if(I2C_RegRead(BMP180_ADDRESS,BMP180_CALVALUE_BASE,c,22u)!=I2C_OK)
        goto finish;
    (void)BMP180_LoadCalibration(c);
finish:
    I2C_TransactionEnd();
}

void BMP180_StartConv(uint8_t code) {
    if(s_ok)
        s_result=I2C_RegWrite(BMP180_ADDRESS,BMP180_CONTROL,&code,1u);
}

void BMP180_GetTemprature(void) {
    uint8_t data[2];
    if (!s_ok || s_result != I2C_OK) return;
    s_result = I2C_RegRead(BMP180_ADDRESS, BMP180_ADCVALUE_BASE, data, sizeof(data));
    if (s_result == I2C_OK && !BMP180_ApplyTemperature(data, 0)) s_result = I2C_ERROR;
}

void BMP180_GetPressure(void) {
    uint8_t data[3];
    if (!s_ok || s_result != I2C_OK) return;
    s_result = I2C_RegRead(BMP180_ADDRESS, BMP180_ADCVALUE_BASE, data, sizeof(data));
    if (s_result == I2C_OK && !BMP180_ApplyPressure(data, 0)) s_result = I2C_ERROR;
}

#if BMP180_USE_FLOAT_API
void BMP180_CalcAltitude(void) {
    const float p=(float)BMP180_AirPressure;
    if(p<=0.0f)return;
    BMP180_AirPressure_kPa=p*0.001f;
    BMP180_Altitude=44330.0f*(1.0f-powf(p/101325.0f,0.1903f));
}
#endif

uint8_t BMP180_ReadSample(void) {
    if(!s_ok || I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    BMP180_StartConv(BMP180_CMD_TEMP);
    if(s_result!=I2C_OK)goto failed;
    Millis_Delay(5u);
    BMP180_GetTemprature();
    if(s_result!=I2C_OK)goto failed;
    BMP180_StartConv(BMP180_CMD_PRESS_OSS3);
    if(s_result!=I2C_OK)goto failed;
    Millis_Delay(26u);
    BMP180_GetPressure();
    if(s_result!=I2C_OK)goto failed;
    I2C_TransactionEnd();
    return I2C_OK;
failed:
    I2C_TransactionEnd();
    return I2C_ERROR;
}

/************************ Async service data conversion ***********************/
char BMP180_TransferPT(struct pt *pt, uint8_t read, uint8_t reg, uint8_t *data,
                       uint16_t count, volatile uint8_t *flag) {
    return I2C_RegTransferPT(pt, BMP180_ADDRESS, read, reg, data, count, flag);
}

uint8_t BMP180_AsyncRead(uint8_t reg,uint8_t *data,uint16_t count,uint8_t *flag) {
    return ASYNC_I2C_Receive(BMP180_ADDRESS,reg,data,count,0u,flag);
}
uint8_t BMP180_AsyncWrite(uint8_t reg,uint8_t *data,uint16_t count,uint8_t *flag) {
    return ASYNC_I2C_Transmit(BMP180_ADDRESS,reg,data,count,0u,flag);
}
uint8_t BMP180_LoadCalibration(const uint8_t *c) {
    if(!c)return 0u;
    ac1=s16(&c[0]);ac2=s16(&c[2]);ac3=s16(&c[4]);ac4=be16(&c[6]);
    ac5=be16(&c[8]);ac6=be16(&c[10]);b1=s16(&c[12]);b2=s16(&c[14]);
    mb=s16(&c[16]);mc=s16(&c[18]);md=s16(&c[20]);
    s_ok=1u;
    return 1u;
}
uint8_t BMP180_ApplyTemperature(const uint8_t *d,int *temp10) {
    int32_t ut,x1,x2,den;
    if(!s_ok || !d)return 0u;
    ut=(int32_t)be16(d);
    x1=((ut-(int32_t)ac6)*(int32_t)ac5)>>15;
    den=x1+(int32_t)md;
    if(!den)return 0u;
    x2=((int32_t)mc*2048)/den;
    b5=x1+x2;
    if(temp10)*temp10=(int)((b5+8)>>4);
#if BMP180_USE_FLOAT_API
    BMP180_AirTemprature=(float)((b5+8)>>4)*0.1f;
#endif
    return 1u;
}
uint8_t BMP180_ApplyPressure(const uint8_t *d,int *pressure_hpa10) {
    int32_t b3,b6,x1,x2,x3,p,up;
    uint32_t b4,b7;
    if(!s_ok || !d)return 0u;
    up=(int32_t)(((uint32_t)d[0]<<16)|((uint32_t)d[1]<<8)|d[2])>>(8u-BMP180_OSS);
    b6=b5-4000;
    x1=((int32_t)b2*((b6*b6)>>12))>>11;
    x2=((int32_t)ac2*b6)>>11;
    x3=x1+x2;
    b3 = (((int32_t)ac1 * 4 + x3) * (1 << BMP180_OSS) + 2) >> 2;
    x1=((int32_t)ac3*b6)>>13;
    x2=((int32_t)b1*((b6*b6)>>12))>>16;
    x3=(x1+x2+2)>>2;
    b4=(uint32_t)((((uint32_t)ac4)*(uint32_t)(x3+32768))>>15);
    if(!b4)return 0u;
    b7=(uint32_t)((int64_t)(up-b3)*(50000u>>BMP180_OSS));
    p=(b7<0x80000000u)?(int32_t)((b7*2u)/b4):(int32_t)((b7/b4)*2u);
    x1=(p>>8)*(p>>8);
    x1=(x1*3038)>>16;
    x2=(-7357*p)>>16;
    BMP180_AirPressure=p+((x1+x2+3791)>>4);
#if BMP180_USE_FLOAT_API
    BMP180_AirPressure_kPa=(float)BMP180_AirPressure*0.001f;
#endif
    if(pressure_hpa10)*pressure_hpa10=(int)(BMP180_AirPressure/10);
    return 1u;
}
