#ifndef PB10A_PM_API_H
#define PB10A_PM_API_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
    /* Maximum state depth: DEEPSLEEP (STOP1); STANDBY is forbidden. */
    void pm_api_init(void);
    void pm_api_poll(void);
    void pm_api_refresh_idle(void);
    void pm_api_wake_from_key(void);
    void pm_api_on_key_irq(void);
    void pm_api_force_sleep(void);
    void pm_api_set_sleep_timeout(int seconds);
    void pm_api_set_sleep_block(uint8_t mask,uint8_t enabled);
    uint8_t pm_api_get_block_mask(void);
    uint8_t pm_api_ui_should_block(void);
    uint8_t pm_api_is_sleeping(void);
    uint8_t pm_api_is_unstable_wake(void);
#ifdef __cplusplus
}
#endif
#endif
