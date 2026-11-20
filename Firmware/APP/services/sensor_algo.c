/* Sensor STOP1 policy.
 * SC7A20: save the actual ODR, power-down acquisition for STOP, restore ODR.
 * MMC5603: preserve ODR/continuous mode only when the IC is found and active.
 * BMP180: already idle between conversions; its calibration stays in RAM.
 *
 * Existing BSP I2C operations wait synchronously for IRQ completion. No
 * millisecond spin waits are added here, but each transaction can block the
 * cooperative scheduler for its own timeout. All failures abort STOP.
 */
#include "sensor_algo.h"
#include "pm_device.h"
#include "sc7a20.h"
#include "mmc5603.h"
#include "bmp180.h"
#include "bsp_i2c.h"
static uint8_t s_sc_suspended,s_sc_odr;
static uint8_t s_mmc_suspended,s_mmc_odr,s_mmc_ctrl2;

static pm_hook_result_t sensor_prepare(void *ctx) {
    (void)ctx;
    return PM_HOOK_DONE; /* sensors may continue during UI_OFF */
}

static pm_hook_result_t sensor_suspend(void *ctx) {
    uint8_t ctrl;
    (void)ctx;
    if(SC7A20_IsInitialized() && !s_sc_suspended) {
        if(I2C_RegRead(SC7A20_I2C_ADDR,SC7A20_CTRG_CTRL1,&ctrl,1u)!=I2C_OK)
            return PM_HOOK_ERROR;
        s_sc_odr=ctrl&SC7A20_CTRL1_ODR_MSK;
        s_sc_suspended=1u; /* allow wake unwind even if write fails */
        SC7A20_SetODR(SC7A20_ODR_POWERDOWN);
        if(I2C_RegRead(SC7A20_I2C_ADDR,SC7A20_CTRG_CTRL1,&ctrl,1u)!=I2C_OK ||
            (ctrl&SC7A20_CTRL1_ODR_MSK)!=SC7A20_ODR_POWERDOWN)
        return PM_HOOK_ERROR;
    }

    if(!s_mmc_suspended && MMC5603_Probe()) {
        /* Avoid resetting an inactive or absent chip just to save microamps. */
        if(MMC5603_ReadReg(MMC5603_REG_ODR,&s_mmc_odr)!=I2C_OK ||
            MMC5603_ReadReg(MMC5603_REG_CTRL2,&s_mmc_ctrl2)!=I2C_OK)
        return PM_HOOK_ERROR;
        if(s_mmc_odr || (s_mmc_ctrl2&MMC5603_CTRL2_CMM_EN)) {
            s_mmc_suspended=1u;
            if(MMC5603_EnterLowPower()!=I2C_OK)return PM_HOOK_ERROR;
        }
    }
    /* BMP180 has no continuous-conversion command in this BSP and is already
     * idle when BMP180_ReadSample() returns. No reset/I2C write is needed. */
    (void)BMP180_IsInitialized();
    return PM_HOOK_DONE;
}

static pm_hook_result_t sensor_resume(void *ctx) {
    uint8_t ctrl,failed=0u;
    (void)ctx;
    if(s_mmc_suspended) {
        if(MMC5603_WriteReg(MMC5603_REG_ODR,s_mmc_odr)!=I2C_OK ||
            MMC5603_WriteReg(MMC5603_REG_CTRL2,s_mmc_ctrl2)!=I2C_OK)
        failed=1u;
        else s_mmc_suspended=0u;
    }
    /* Always try to restore SC7A20, even when MMC5603 failed. */
    if(s_sc_suspended) {
        SC7A20_SetODR(s_sc_odr);
        if(I2C_RegRead(SC7A20_I2C_ADDR,SC7A20_CTRG_CTRL1,&ctrl,1u)!=I2C_OK ||
            (ctrl&SC7A20_CTRL1_ODR_MSK)!=s_sc_odr)
        failed=1u;
        else s_sc_suspended=0u;
    }
    return failed?PM_HOOK_ERROR:PM_HOOK_DONE;
}

static const pm_device_node_t s_node={
    "sensors",sensor_prepare,sensor_suspend,sensor_resume,0,80u,0,0
};
void SensorAlgo_Init(void){(void)pm_device_register(&s_node);}
