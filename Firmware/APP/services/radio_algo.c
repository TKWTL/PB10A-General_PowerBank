/* RDA5807 low-power policy. Force ENABLE=0 after I2C initialization.
 * MCO=32.768 kHz is provided only for the power-down transaction and while
 * an explicitly started radio session is active. Before UI_OFF/STOP1 power
 * down even a receiver left in RADIO_ERROR. Wake leaves MCO disabled. */
#include "radio_algo.h"
#include "radio_app.h"
#include "pm_device.h"
static uint8_t s_boot_sleep_ack;
static pm_hook_result_t radio_prepare(void *ctx)
{
    (void)ctx;
    Radio_AppStop(); /* Also retries power-down after RADIO_ERROR. */
    return PM_HOOK_DONE;
}
static pm_hook_result_t radio_resume(void *ctx)
{
    (void)ctx;
    return PM_HOOK_DONE; /* MCO remains off until Radio_AppStart(). */
}
static const pm_device_node_t s_node={
    "rda5807",radio_prepare,0,radio_resume,0,110u,0,0
};
void RadioAlgo_Init(void)
{
    s_boot_sleep_ack=Radio_AppInitLowPower();
    (void)pm_device_register(&s_node);
}
uint8_t RadioAlgo_BootSleepAcknowledged(void)
{return s_boot_sleep_ack;}
