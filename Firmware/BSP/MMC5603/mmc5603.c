/* MMC5603NJ bring-up phase 1.
 * Register names -> common I2C Read/Write -> ID check -> low power.
 * All multi-step operations hold one transaction lock across I2C frames.
 */
#include "mmc5603.h"
#include "bsp_i2c.h"
uint8_t MMC5603_ReadReg(uint8_t reg,uint8_t *value) {
    if(!value)return I2C_ERROR;
    return I2C_RegRead(MMC5603_I2C_ADDR,reg,value,1u);
}

uint8_t MMC5603_WriteReg(uint8_t reg,uint8_t value) {
    return I2C_RegWrite(MMC5603_I2C_ADDR,reg,&value,1u);
}

uint8_t MMC5603_ReadID(uint8_t *id) {
    return MMC5603_ReadReg(MMC5603_REG_PRODUCT_ID,id);
}

uint8_t MMC5603_Probe(void) {
    uint8_t id=0u;
    return (MMC5603_ReadID(&id)==I2C_OK&&id==MMC5603_PRODUCT_ID)?1u:0u;
}

uint8_t MMC5603_EnterLowPower(void) {
    uint8_t ok=I2C_ERROR;
    if(I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    /* Power-down: disable continuous measurement and clear ODR.
     * Do not issue a soft reset: preserves any existing calibration. */
    if(MMC5603_WriteReg(MMC5603_REG_CTRL2,0u)==I2C_OK &&
        MMC5603_WriteReg(MMC5603_REG_ODR,0u)==I2C_OK)
    ok=I2C_OK;
    I2C_TransactionEnd();
    return ok;
}

uint8_t MMC5603_StartSingleMeasurement(void) {
    /* TM_M sets a single-shot magnetic measurement; self-clearing. */
    return MMC5603_WriteReg(MMC5603_REG_CTRL0,MMC5603_CTRL0_TM_M);
}
