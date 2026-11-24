/* OLED SPI DMA power sequencing and STM32 STOP1 clock/tick restore. */
#include "pm_device.h"
#include "ssd1306_display.h"
#include "i2c.h"
#include "spi.h"
#include "millis.h"
#include "main.h"
#include "sc7a20_algo.h"
#include "radio_algo.h"
#include "pm_api.h"
#include "rtc.h"
#define OLED_PM_TIMEOUT_MS 500u
/* Production Radio battery inspection period: 30 minutes from 1Hz ck_spre. */
#define RADIO_WUT_SECONDS 1800u
void SystemClock_Config(void);
static uint32_t adc_internal,oled_started;
static uint8_t suspended,oled_target,oled_valid,s_radio_wut_armed;
static pm_hook_result_t oled_power(uint8_t off) {
    uint32_t now=Millis();
    if(!oled_valid || oled_target!=off) {
        oled_target=off;oled_valid=1;oled_started=now;
        SSD1306_Display_RequestPowerSave(off);
    }
    if(SSD1306_Display_PowerSettled(off))return PM_HOOK_DONE;
    if(SSD1306_Display_InitState()==SSD1306_INIT_FAILED ||
        (uint32_t)(now-oled_started)>OLED_PM_TIMEOUT_MS)
    return PM_HOOK_ERROR;
    return PM_HOOK_PENDING;
}

static pm_hook_result_t oled_prepare(void *ctx)
{(void)ctx;return oled_power(1);}
static pm_hook_result_t oled_resume(void *ctx)
{(void)ctx;return oled_power(0);}
static void radio_wut_disarm(void) {
    if(s_radio_wut_armed) {
        /* HAL deactivation stops WUTE/WUTIE but does not explicitly
         * clear RTC_SR.WUTF. The HAL IRQ handler normally clears a fired
         * WUT flag; also clear it on cancellation from another wake
         * source. Do NOT clear the software source latch here: PM must
         * still inspect it to distinguish RTC patrol from a user IRQ. */
        if(HAL_RTCEx_DeactivateWakeUpTimer(&hrtc)==HAL_OK) {
            __HAL_RTC_WAKEUPTIMER_CLEAR_FLAG(&hrtc,RTC_FLAG_WUTF);
            s_radio_wut_armed=0u;
        }
    }
}
static pm_hook_result_t stm32_suspend(void *ctx) {
    (void)ctx;
    if(suspended)return PM_HOOK_DONE;
    if (I2C_BusIsLocked() || SSD1306_Display_FrameBusy() ||
        SPI_DmaTxState() != SPI_DMA_IDLE)
        return PM_HOOK_PENDING;
    /* Arm the timer while HAL Tick still runs; the RTC IRQ only latches
     * its source. A failed arm rejects STOP1 instead of leaving an active
     * FM receiver without periodic battery monitoring. */
    if(RadioAlgo_BackgroundWakeNeeded()) {
        if(HAL_RTCEx_SetWakeUpTimer_IT(&hrtc,RADIO_WUT_SECONDS-1u,
            RTC_WAKEUPCLOCK_CK_SPRE_16BITS)!=HAL_OK)
            return PM_HOOK_ERROR;
        s_radio_wut_armed=1u;
    }
    adc_internal = PB_ADC_COMMON->CCR & (ADC_CCR_VREFEN | ADC_CCR_TSEN);
    PB_ADC_COMMON->CCR &= ~(ADC_CCR_VREFEN | ADC_CCR_TSEN);
    HAL_SuspendTick();suspended=1;return PM_HOOK_DONE;
}

static void stm32_restore(void) {
    PB_ADC_COMMON->CCR = (PB_ADC_COMMON->CCR & ~(ADC_CCR_VREFEN | ADC_CCR_TSEN)) | adc_internal;
    HAL_ResumeTick();suspended=0;
}

static void stm32_deepsleep(void *ctx) {
    (void)ctx;
    /* PB5 is level-low when the sensor has already signalled a wake.
     * A cleared edge pending bit must not strand STOP1 on a held-low pin. */
    /* Any EXTI that arrived after the PM's last poll must prevent WFI:
     * the edge might otherwise be cleared before STOP1 is entered. */
    if(!pm_api_rtc_wakeup_pending() && !pm_api_rtc_alarm_pending() &&
        !pm_api_external_wake_pending() &&
        (!SC7A20Algo_WakeIRQ() || LL_GPIO_IsInputPinSet(INT_GPIO_Port,INT_Pin)))
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON,PWR_STOPENTRY_WFI);
    SystemClock_Config();
    RadioAlgo_RestoreClock();
    if(suspended)stm32_restore();
    /* Stop the one-shot WUT after any STOP1 exit. PM decides whether the
     * wake goes to RUN or to a selective UI_OFF background inspection. */
    radio_wut_disarm();
}

static pm_hook_result_t stm32_resume(void *ctx)
{
    (void)ctx;
    if(suspended)stm32_restore();
    radio_wut_disarm();
    return PM_HOOK_DONE;
}
static const pm_device_node_t oled_node={
    "oled",oled_prepare,0,oled_resume,0,230u,0,0
};
static const pm_device_node_t platform_node={
    "stm32g070",0,stm32_suspend,stm32_resume,0,250u,stm32_deepsleep,0
};
void pm_device_register_builtin(void) {
    (void)pm_device_register(&oled_node);
    (void)pm_device_register(&platform_node);
}
