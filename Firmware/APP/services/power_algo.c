/* SW6306 power policy: preserve charging, PD contracts and live outputs.
 * Only request the IC's low-power register mode when its live port status
 * confirms every connection is idle. Never force charge/discharge OFF. */
#include "power_algo.h"
#include "pm_device.h"
#include "sw6306.h"

static uint8_t s_sw_low_power;
static pm_hook_result_t sw_prepare(void *ctx)
{
 (void)ctx;
 return PM_HOOK_DONE; /* UI_OFF does not stop SW6306 */
}
static pm_hook_result_t sw_suspend(void *ctx)
{
 (void)ctx;
 if(s_sw_low_power || !SW6306_IsInitialized())return PM_HOOK_DONE;
 /* Failure/unknown status is treated as 'active': do not touch charge path.
  * SW6306 can keep its standalone charging/PD logic while MCU is in STOP1. */
 if(SW6306_TransportPortsIdle())
 {
  SW6306_LPSet(); /* 0x23 = 0; chip returns to autonomous low-power mode */
  s_sw_low_power=1u;
 }
 return PM_HOOK_DONE;
}
static pm_hook_result_t sw_resume(void *ctx)
{
 (void)ctx;
 if(!s_sw_low_power)return PM_HOOK_DONE;
 if(!SW6306_WakeAndUnlock())return PM_HOOK_ERROR;
 s_sw_low_power=0u;
 return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
 "sw6306",sw_prepare,sw_suspend,sw_resume,0,90u,0,0
};
void PowerAlgo_Init(void){(void)pm_device_register(&s_node);}
