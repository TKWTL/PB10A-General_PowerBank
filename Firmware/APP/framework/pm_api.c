/* PB10A cooperative power manager. State transitions from PB02B/IH01A.
 * Low-power hooks may asynchronously return PENDING.
 */
#include "pm_api.h"
#include "pm_controller.h"
#include "pm_device.h"
#include "pm_sleep_timer.h"
#include "millis.h"
#include "main.h"
#include "power_algo.h"
#include "radio_algo.h"
#define PM_DEFAULT_TIMEOUT_MS 60000u
static pm_controller_t s_ctrl;
static uint8_t s_ready,s_block_mask,s_paused,s_forced;
static volatile uint8_t s_irq_events;
static volatile uint8_t s_rtc_wakeup_event;
static volatile uint8_t s_rtc_alarm_events; /* RTC calendar Alarm A/B IRQs */
static uint8_t s_app_rtc_alarm_events;      /* foreground consumption */
static uint8_t s_app_events; /* PM-dispatched sources for foreground services */
static uint8_t s_refresh_request,s_wake_guard;
static uint32_t s_wake_at;
/* RTC-only background wake: CPU/clock/tick active, but MiaoUI and OLED
 * remain in UI_OFF. Only SW6306 and RDA5807 are resumed selectively. */
typedef enum {
    PM_BG_NONE=0,PM_BG_START,PM_BG_SW_RESUME,PM_BG_RADIO,
    PM_BG_SW_SUSPEND
} pm_bg_stage_t;
static pm_bg_stage_t s_bg_stage;
static uint32_t s_bg_started_at;
#define PM_BG_MAX_MS 20000u
void pm_api_init(void) {
    if(s_ready)return;
    pm_controller_init(&s_ctrl,PM_DEFAULT_TIMEOUT_MS);
    pm_controller_mark_ui_active(&s_ctrl,1u);
    pm_device_register_all();
    s_ready=1u;
}

void pm_api_refresh_idle(void) {
    if(!pm_sleep_timer_is_forced())s_refresh_request=1u;
}

/* RTC_TAMP interrupt callback: latch WUT only. The 30-minute
 * background check is dispatched in the PM thread, without OLED power-up. */
void HAL_RTCEx_WakeUpTimerEventCallback(RTC_HandleTypeDef *rtc) {
    if(rtc && rtc->Instance==RTC)s_rtc_wakeup_event=1u;
}
void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *rtc) {
    if(rtc && rtc->Instance==RTC)s_rtc_alarm_events|=PM_RTC_ALARM_A;
}
void HAL_RTCEx_AlarmBEventCallback(RTC_HandleTypeDef *rtc) {
    if(rtc && rtc->Instance==RTC)s_rtc_alarm_events|=PM_RTC_ALARM_B;
}
void pm_api_on_rtc_wakeup_irq(void){s_rtc_wakeup_event=1u;}
uint8_t pm_api_rtc_wakeup_pending(void){return s_rtc_wakeup_event;}
uint8_t pm_api_rtc_alarm_pending(void){return s_rtc_alarm_events!=0u;}
uint8_t pm_api_external_wake_pending(void){return s_irq_events!=0u;}
uint8_t pm_api_take_rtc_alarm_events(void) {
    uint32_t primask=__get_PRIMASK();
    uint8_t events;
    __disable_irq();
    events=s_app_rtc_alarm_events;
    s_app_rtc_alarm_events=0u;
    __set_PRIMASK(primask);
    return events;
}

void pm_api_on_exti_event(uint8_t source) {
    uint32_t primask=__get_PRIMASK();
    /* The same flag can also be set by foreground pre-STOP level checks. */
    __disable_irq();
    s_irq_events|=source;
    __set_PRIMASK(primask);
}
uint8_t pm_api_take_events(void) {
    uint32_t primask=__get_PRIMASK();
    uint8_t events;
    __disable_irq();
    events=s_app_events;
    s_app_events=0u;
    __set_PRIMASK(primask);
    return events;
}
void pm_api_wake_from_key(void){pm_api_on_exti_event(PM_WAKE_KEY);}
void pm_api_on_key_irq(void){pm_api_on_exti_event(PM_WAKE_KEY);}
void pm_api_force_sleep(void) {
    pm_api_init();
    s_refresh_request=0u;s_forced=1u;
    pm_sleep_timer_force_expire();
    pm_controller_mark_ui_active(&s_ctrl,0u);
}

void pm_api_set_sleep_timeout(int seconds) {
    pm_api_init();
    if(seconds<=0)pm_sleep_timer_disable();
    else pm_sleep_timer_set((uint32_t)seconds*1000u);
}

void pm_api_set_sleep_block(uint8_t mask,uint8_t enabled) {
    if(enabled)s_block_mask|=mask;
    else s_block_mask&=(uint8_t)~mask;
}

uint8_t pm_api_get_block_mask(void){return s_block_mask;}
uint8_t pm_api_ui_should_block(void)
{return s_ready && pm_controller_is_ui_blocked(&s_ctrl);}
uint8_t pm_api_is_sleeping(void)
{return s_ready && s_ctrl.state!=PM_STATE_RUN;}
uint8_t pm_api_is_unstable_wake(void)
{return s_wake_guard && (uint32_t)(Millis()-s_wake_at)<200u;}
static void pm_background_abort_to_run(void) {
    /* User activity or inspection failure: normal reverse-order PM
     * resume handles all devices. A running SW child finishes before
     * its device's resume hook can claim the shared I2C bus. */
    PowerAlgo_BackgroundAbort();
    RadioAlgo_AbortBackgroundInspection();
    s_bg_stage=PM_BG_NONE;
    pm_controller_notify_wake(&s_ctrl);
    pm_controller_mark_ui_active(&s_ctrl,1u);
}
static void pm_background_poll(void) {
    pm_hook_result_t result;
    if((uint32_t)(Millis()-s_bg_started_at)>PM_BG_MAX_MS) {
        RadioAlgo_RequestFailSafeOff();
        pm_background_abort_to_run();
        /* Resume the full UI instead of silently sleeping without a
         * successfully completed low-battery inspection. */
        return;
    }
    switch(s_bg_stage) {
    case PM_BG_START:
        if(RadioAlgo_BackgroundInspectionNeedsBattery())
            s_bg_stage=PM_BG_SW_RESUME;
        else s_bg_stage=PM_BG_RADIO;
        break;
    case PM_BG_SW_RESUME:
        result=PowerAlgo_BackgroundResume();
        if(result==PM_HOOK_ERROR) {
            RadioAlgo_RequestFailSafeOff();
            pm_background_abort_to_run();
        }
        else if(result==PM_HOOK_DONE) {
            RadioAlgo_RequestBatteryInspection();
            s_bg_stage=PM_BG_RADIO;
        }
        break;
    case PM_BG_RADIO:
        if(RadioAlgo_BackgroundInspectionComplete())
            s_bg_stage=PM_BG_SW_SUSPEND;
        break;
    case PM_BG_SW_SUSPEND:
        result=PowerAlgo_BackgroundSuspend();
        if(result==PM_HOOK_ERROR)pm_background_abort_to_run();
        else if(result==PM_HOOK_DONE) {
            RadioAlgo_EndBackgroundInspection();
            s_bg_stage=PM_BG_NONE;
            /* Resume the existing UI_OFF -> SLEEP_PREPARE -> STOP1
             * policy on the next 50ms PM tick. No OLED resume occurred. */
        }
        break;
    default:break;
    }
}
void pm_api_poll(void) {
    pm_power_state_t previous,next;
    uint8_t events,rtc_wakeup,rtc_alarms;
    uint32_t primask;
    pm_api_init();
    /* Consume once per PM poll without racing an EXTI that writes the flag. */
    primask=__get_PRIMASK();
    __disable_irq();
    events=s_irq_events;
    s_irq_events=0u;
    rtc_alarms=s_rtc_alarm_events;
    s_rtc_alarm_events=0u;
    /* External GPIO and calendar alarm events must retain their original
     * sources for the relevant foreground services. WUT stays separate:
     * it must not refresh the user-idle timer or light the OLED. */
    s_app_events|=events;
    s_app_rtc_alarm_events|=rtc_alarms;
    __set_PRIMASK(primask);
    if(s_bg_stage!=PM_BG_NONE) {
        if(events || rtc_alarms)pm_background_abort_to_run();
        else {
            pm_background_poll();
            if(s_bg_stage!=PM_BG_NONE)return;
            /* If background work completed this tick, proceed through
             * ordinary PM policy next tick to avoid a mixed transition. */
            return;
        }
    }
    if(events || rtc_alarms) {
        /* Physical EXTI and calendar alarms are explicit RUN wake sources.
         * A future BEEP task can choose a different alarm UI policy. */
        uint8_t waking=pm_controller_is_ui_blocked(&s_ctrl)?1u:0u;
        pm_sleep_timer_clear_force();
        s_forced=0u;
        s_refresh_request=0u;
        pm_controller_refresh_idle(&s_ctrl);
        if(waking){s_wake_guard=1u;s_wake_at=Millis();}
    }
    else if(s_refresh_request && !s_forced) {
        s_refresh_request=0u;
        pm_controller_refresh_idle(&s_ctrl);
    }
    if(s_block_mask) {
        if(!s_paused){pm_controller_pause_idle(&s_ctrl);s_paused=1u;}
    }
    else if(s_paused) {
        pm_controller_resume_idle(&s_ctrl);s_paused=0u;
    }
    if(pm_sleep_timer_expired())pm_controller_mark_ui_active(&s_ctrl,0u);
    previous=s_ctrl.state;
    next=pm_controller_step(&s_ctrl);
    /* A device ERROR can cancel a forced sleep before STOP1. Clear the
     * forced-expiry latch once recovery reaches RUN, or PM will loop
     * indefinitely back into UI_OFF. */
    if(s_forced && next==PM_STATE_RUN && !s_ctrl.transition_pending) {
        pm_sleep_timer_clear_force();
        s_forced=0u;
    }
    if(previous!=PM_STATE_DEEPSLEEP && next==PM_STATE_DEEPSLEEP) {
        /* Registered STM32G070 node: STOP1 then restore clocks/tick/ADC. */
        pm_device_enter_deepsleep();
        /* STOP1 has ended physically, but the RTC-only path must not
         * activate OLED or force the software state to RUN. */
        /* The platform has restored clocks and HAL Tick. Record the RTC
         * event separately from external activity so it does not imitate
         * a button press or post a UI navigation event. */
        primask=__get_PRIMASK();
        __disable_irq();
        rtc_wakeup=s_rtc_wakeup_event;
        s_rtc_wakeup_event=0u;
        __set_PRIMASK(primask);
        if(rtc_wakeup && !pm_api_external_wake_pending() &&
            !pm_api_rtc_alarm_pending()) {
            /* WUT was armed for ON or inserted-AUTO Radio. Check CURRENT
             * MIC_INDET after wake for AUTO, not an old insertion latch.
             * If Alarm A/B or EXTI also fired, visible RUN takes priority. */
            s_bg_stage=PM_BG_START;
            s_bg_started_at=Millis();
            pm_controller_enter_background_check(&s_ctrl);
            RadioAlgo_BeginBackgroundInspection();
            return;
        }
        pm_controller_notify_wake(&s_ctrl);
        pm_controller_mark_ui_active(&s_ctrl,1u);
        pm_sleep_timer_clear_force();
        s_forced=0u;
        s_wake_guard=1u;s_wake_at=Millis();
    }
}
