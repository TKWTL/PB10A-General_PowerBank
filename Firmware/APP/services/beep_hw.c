/* PB10A PB1 BEEP pin is already TIM3_CH4 AF1, configured by CubeMX.
* TIM3 clock=64MHz; PSC=63 -> 1MHz; hardware PWM continues in foreground
* without any busy-wait or 1ms SysTick tone synthesis. */
#include "beep_hw.h"
#include "tim.h"
static uint8_t s_running;
void beep_hw_init(void) {
    s_running=0u;
    __HAL_TIM_SET_PRESCALER(&htim3,63u);
    __HAL_TIM_SET_AUTORELOAD(&htim3,999u);
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,0u);
    __HAL_TIM_SET_COUNTER(&htim3,0u);
    htim3.Instance->EGR=TIM_EGR_UG;
}
void beep_hw_stop(void) {
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,0u);
    if(s_running){(void)HAL_TIM_PWM_Stop(&htim3,TIM_CHANNEL_4);s_running=0u;}
}
void beep_hw_set_arr(uint16_t arr) {
    if(arr==0u){beep_hw_stop();return;}
    __HAL_TIM_SET_AUTORELOAD(&htim3,arr);
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,((uint32_t)arr+1u)/2u);
    __HAL_TIM_SET_COUNTER(&htim3,0u);
    htim3.Instance->EGR=TIM_EGR_UG;
    if(!s_running) {
        if(HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4)==HAL_OK)s_running=1u;
        else beep_hw_stop();
    }
}
