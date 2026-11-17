/* PB10A cooperative power manager. State transitions from PB02B/IH01A.
 * No external peripheral low-power callbacks are active yet.
 */
#include "pm_api.h"
#include "pm_controller.h"
#include "pm_device.h"
#include "pm_sleep_timer.h"
#include "ssd1306_display.h"
#include "spi.h"
#include "main.h"
#define PM_DEFAULT_TIMEOUT_MS 60000u
static pm_controller_t s_ctrl;
static uint8_t s_ready,s_block_mask,s_paused,s_forced;
static volatile uint8_t s_key_irq;
static uint8_t s_refresh_request,s_wake_guard;
static uint32_t s_wake_at;
void pm_api_init(void)
{
    if(s_ready)return;
    pm_controller_init(&s_ctrl,PM_DEFAULT_TIMEOUT_MS);
    pm_controller_mark_ui_active(&s_ctrl,1u);
    pm_device_register_all();
    s_ready=1u;
}
void pm_api_refresh_idle(void)
{
    if(!pm_sleep_timer_is_forced())s_refresh_request=1u;
}
void pm_api_wake_from_key(void){s_key_irq=1u;}
void pm_api_on_key_irq(void){s_key_irq=1u;}
void pm_api_force_sleep(void)
{
    pm_api_init();
    s_refresh_request=0u;s_forced=1u;
    pm_sleep_timer_force_expire();
    pm_controller_mark_ui_active(&s_ctrl,0u);
}
void pm_api_set_sleep_timeout(int seconds)
{
    pm_api_init();
    if(seconds<=0)pm_sleep_timer_disable();
    else pm_sleep_timer_set((uint32_t)seconds*1000u);
}
void pm_api_set_sleep_block(uint8_t mask,uint8_t enabled)
{
    if(enabled)s_block_mask|=mask;
    else s_block_mask&=(uint8_t)~mask;
}
uint8_t pm_api_get_block_mask(void){return s_block_mask;}
uint8_t pm_api_ui_should_block(void)
{return s_ready && pm_controller_is_ui_blocked(&s_ctrl);}
uint8_t pm_api_is_sleeping(void)
{return s_ready && s_ctrl.state!=PM_STATE_RUN;}
uint8_t pm_api_is_unstable_wake(void)
{return s_wake_guard && (uint32_t)(HAL_GetTick()-s_wake_at)<200u;}
void pm_api_poll(void)
{
    pm_power_state_t previous,next;
    pm_api_init();
    if(s_key_irq)
    {
        s_key_irq=0u;
        pm_sleep_timer_clear_force();
        s_forced=0u;
        pm_controller_refresh_idle(&s_ctrl);
        s_wake_guard=1u;s_wake_at=HAL_GetTick();
    }
    else if(s_refresh_request && !s_forced)
    {
        s_refresh_request=0u;
        pm_controller_refresh_idle(&s_ctrl);
    }
    if(s_block_mask)
    {
        if(!s_paused){pm_controller_pause_idle(&s_ctrl);s_paused=1u;}
    }
    else if(s_paused)
    {
        pm_controller_resume_idle(&s_ctrl);s_paused=0u;
    }
    if(pm_sleep_timer_expired())pm_controller_mark_ui_active(&s_ctrl,0u);
    /* Do not enter STOP while the OLED still owns the u8g2 framebuffer
     * or the SPI HAL is processing/aborting a DMA operation. */
    if(s_ctrl.state==PM_STATE_SLEEP_PREPARE &&
       (SSD1306_Display_FrameBusy() || SPI1_DmaTxState()!=SPI1_DMA_IDLE))
        pm_controller_refresh_idle(&s_ctrl);
    previous=s_ctrl.state;
    next=pm_controller_step(&s_ctrl);
    if(previous!=PM_STATE_DEEPSLEEP && next==PM_STATE_DEEPSLEEP)
    {
        /* Registered STM32G070 node: STOP1 then restore clocks/tick/ADC. */
        pm_device_enter_deepsleep();
        pm_controller_notify_wake(&s_ctrl);
        pm_controller_mark_ui_active(&s_ctrl,1u);
        pm_sleep_timer_clear_force();
        s_forced=0u;
        s_wake_guard=1u;s_wake_at=HAL_GetTick();
    }
}
