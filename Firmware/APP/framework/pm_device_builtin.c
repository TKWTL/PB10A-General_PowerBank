/* Platform power hooks, based on PB02B device model and Collar-Radio
 * SystemSleep(): lower STM32 consumption, then restore after STOP1.
 * External devices are intentionally not registered for this iteration.
 */
#include "pm_device.h"
#include "main.h"
void SystemClock_Config(void);
static uint32_t s_adc_internal;
static uint8_t s_suspended;
static void stm32_suspend(void *ctx)
{
    (void)ctx;
    if(s_suspended)return;
    s_adc_internal=ADC1_COMMON->CCR & (ADC_CCR_VREFEN|ADC_CCR_TSEN);
    ADC1_COMMON->CCR &= ~(ADC_CCR_VREFEN|ADC_CCR_TSEN);
    HAL_SuspendTick();
    s_suspended=1u;
}
static void stm32_restore(void)
{
    ADC1_COMMON->CCR=(ADC1_COMMON->CCR & ~(ADC_CCR_VREFEN|ADC_CCR_TSEN))
                     |s_adc_internal;
    HAL_ResumeTick();
    s_suspended=0u;
}
static void stm32_deepsleep(void *ctx)
{
    (void)ctx;
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON,PWR_STOPENTRY_WFI);
    SystemClock_Config();
    stm32_restore();
}
static void stm32_resume(void *ctx)
{
    (void)ctx;
    if(s_suspended)stm32_restore();
}
static const pm_device_node_t s_platform={
    "stm32g070",0,stm32_suspend,stm32_resume,0,250,stm32_deepsleep,0
};
void pm_device_register_builtin(void)
{
    (void)pm_device_register(&s_platform);
}
