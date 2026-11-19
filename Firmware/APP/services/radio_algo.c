/* RDA5807 is demand-driven by the WORD application.
 * Before UI_OFF, power down an active radio and stop the LSE MCO output;
 * after wake, Radio_AppRun() reactivates it only if the page is entered.
 * Do not automatically start FM on every device wake. */
#include "radio_algo.h"
#include "radio_app.h"
#include "pm_device.h"
static pm_hook_result_t radio_prepare(void *ctx)
{
 (void)ctx;
 if(Radio_AppIsActive())Radio_AppStop();
 return PM_HOOK_DONE;
}
static pm_hook_result_t radio_resume(void *ctx)
{
 (void)ctx;
 return PM_HOOK_DONE; /* App chooses when to turn FM on again */
}
static const pm_device_node_t s_node={
 "rda5807",radio_prepare,0,radio_resume,0,110u,0,0
};
void RadioAlgo_Init(void){(void)pm_device_register(&s_node);}
