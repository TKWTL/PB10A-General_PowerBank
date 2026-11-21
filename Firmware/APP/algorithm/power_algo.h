#ifndef PB10A_POWER_ALGO_H
#define PB10A_POWER_ALGO_H
#include <stdint.h>
#include "ui_conf.h"
#include "coroOS.h"
#include "pm_device.h"
/* SW6306 plus auxiliary A-port PA6 and ADC1 DMA telemetry. */
/* Task-facing SW6306 monitor, based on PB02B 100ms staggered LOAD.
 * The displayed BUS voltage/current is the shared SW6306 BUS ADC, not
 * individual per-port current measurements. All getters read RAM only. */
typedef struct {
    uint16_t vbus_mv,ibus_ma,vbat_mv,ibat_ma;
    uint8_t capacity_pct,capacity_valid;
    uint8_t initialized,valid,charging,discharging;
    uint8_t c1_on,a1_on,port_busy;
    const char *protocol; /* static SW6306 driver string, read only */
    uint32_t sample_ms;
} power_monitor_t;
void PowerAlgo_Init(void);
/* SW6306 owner thread: cooperative Init/ADC/Status/Port/Power/Capacity. */
char PowerAlgo_SW6306Thread(coro_pt_t *pt);
void PowerAlgo_GetMonitor(power_monitor_t *out);
/* Radio WUT inspection: requests are serviced by the existing SW6306
 * coroutine, never from RTC ISR. Take returns 0=pending/not available,
 * 1=both fresh ADC and coulomb/capacity data valid, 2=I2C failure.
 * A result is consumed once; voltage is millivolts of the 5S pack. */
void PowerAlgo_RequestRadioBatteryCheck(void);
uint8_t PowerAlgo_TakeRadioBatteryCheck(uint16_t *vbat_mv,uint8_t *capacity_pct);
/* Background WUT path: wake SW6306 only, never OLED or other nodes.
 * Reuses the SW6306 PM hooks and the existing single SW6306 coroOS actor. */
pm_hook_result_t PowerAlgo_BackgroundResume(void);
pm_hook_result_t PowerAlgo_BackgroundSuspend(void);
void PowerAlgo_BackgroundAbort(void);
/* ADC_RTC_AuxAThread alone owns ADC calibration and DMA sampling.
 * Sixteen four-channel conversions, separated by 25 ms per 500 ms
 * burst, are averaged as raw ADC codes before voltage/temperature scaling. */
#define POWER_ADC_AVERAGE_SAMPLES 16u
void PowerAlgo_AdcInit(void);
void PowerAlgo_AdcBatchBegin(void);
uint8_t PowerAlgo_AdcIsSuspended(void);
uint8_t PowerAlgo_AdcBeginScan(void);
uint8_t PowerAlgo_AdcScanSettled(void);
void PowerAlgo_AuxA_Service(void);
uint8_t PowerAlgo_AuxAIsInserted(void);
uint8_t PowerAlgo_AuxAIsEnabled(void);
void PowerAlgo_AddStatisticsItems(ui_page_t *page);
void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c10,uint8_t *valid);
#endif
