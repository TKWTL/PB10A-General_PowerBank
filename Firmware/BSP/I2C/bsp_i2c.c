/* PB10A I2C1 blocking transaction adapter for PB02B and Collar-Radio
 * register drivers. Do not invoke from ISR or concurrent threads. */
#include "bsp_i2c.h"
#include "i2c.h"
#define PB10A_I2C_TIMEOUT_MS 20u
uint8_t I2C_RegWrite(uint16_t a,uint8_t r,const uint8_t *p,uint16_t n)
{
    if(!p || !n)return I2C_ERROR;
    return HAL_I2C_Mem_Write(&hi2c1,a,r,I2C_MEMADD_SIZE_8BIT,
                             (uint8_t *)(void *)p,n,PB10A_I2C_TIMEOUT_MS)==HAL_OK?I2C_OK:I2C_ERROR;
}
uint8_t I2C_RegRead(uint16_t a,uint8_t r,uint8_t *p,uint16_t n)
{
    if(!p || !n)return I2C_ERROR;
    return HAL_I2C_Mem_Read(&hi2c1,a,r,I2C_MEMADD_SIZE_8BIT,
                            p,n,PB10A_I2C_TIMEOUT_MS)==HAL_OK?I2C_OK:I2C_ERROR;
}
uint8_t I2C_Transmit(uint16_t a,const uint8_t *p,uint16_t n)
{
    if(!p || !n)return I2C_ERROR;
    return HAL_I2C_Master_Transmit(&hi2c1,a,(uint8_t *)(void *)p,n,
                                    PB10A_I2C_TIMEOUT_MS)==HAL_OK?I2C_OK:I2C_ERROR;
}
uint8_t I2C_Receive(uint16_t a,uint8_t *p,uint16_t n)
{
    if(!p || !n)return I2C_ERROR;
    return HAL_I2C_Master_Receive(&hi2c1,a,p,n,
                                   PB10A_I2C_TIMEOUT_MS)==HAL_OK?I2C_OK:I2C_ERROR;
}
uint8_t I2C_IsReady(uint16_t a)
{
    return HAL_I2C_IsDeviceReady(&hi2c1,a,1u,2u)==HAL_OK?1u:0u;
}
