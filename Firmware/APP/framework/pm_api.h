#ifndef PB10A_PM_API_H
#define PB10A_PM_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Maximum state depth: DEEPSLEEP (STOP1); STANDBY is forbidden. */
/* A single ISR-to-PM pending bitmask. IRQs never run application logic. */
#define PM_WAKE_KEY        0x01u
#define PM_WAKE_SC7A20 0x02u /* PB5 shared SC7A20 / SW6306 interrupt */
#define PM_WAKE_MIC_INDET  0x04u
#define PM_WAKE_SUB_INDET  0x08u
void pm_api_on_exti_event(uint8_t source);
/* RTC events use distinct software latches, not PM_WAKE_* GPIO bits.
 * WUT requests a silent background inspection; A/B are calendar
 * alarm sources. RTC_TAMP_IRQHandler dispatches both HAL IRQ handlers. */
#define PM_RTC_ALARM_A 0x01u
#define PM_RTC_ALARM_B 0x02u
void pm_api_on_rtc_wakeup_irq(void);
uint8_t pm_api_rtc_wakeup_pending(void);
uint8_t pm_api_rtc_alarm_pending(void);
/* One-shot foreground alarm events (bitmask of PM_RTC_ALARM_A/B). */
uint8_t pm_api_take_rtc_alarm_events(void);
void pm_api_clear_rtc_alarm_events(uint8_t mask);
uint8_t pm_api_external_wake_pending(void);
/* Application consumer receives a separate copy of each PM-dispatched source. */
uint8_t pm_api_take_events(void);
void pm_api_init(void);
void pm_api_poll(void);
void pm_api_refresh_idle(void);
void pm_api_wake_from_key(void);
void pm_api_on_key_irq(void);
void pm_api_force_sleep(void);
void pm_api_set_sleep_timeout(int seconds);
#define PM_SLEEP_BLOCK_ALARM 0x80u
void pm_api_set_sleep_block(uint8_t mask,uint8_t enabled);
uint8_t pm_api_get_block_mask(void);
uint8_t pm_api_ui_should_block(void);
uint8_t pm_api_is_sleeping(void);
uint8_t pm_api_is_unstable_wake(void);
#ifdef __cplusplus
}
#endif
#endif
