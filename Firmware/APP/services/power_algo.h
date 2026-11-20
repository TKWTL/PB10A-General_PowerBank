#ifndef PB10A_POWER_ALGO_H
#define PB10A_POWER_ALGO_H
#include <stdint.h>
#include "ui_conf.h"
#include "coroOS.h"
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
uint8_t PowerAlgo_AdcIsSuspended(void);
uint8_t PowerAlgo_AdcBeginScan(void);
uint8_t PowerAlgo_AdcScanSettled(void);
void PowerAlgo_AddStatisticsItems(ui_page_t *page);
void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c,uint8_t *valid);
#endif
