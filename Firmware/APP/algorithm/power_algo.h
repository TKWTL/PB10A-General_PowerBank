#ifndef PB10A_POWER_ALGO_H
#define PB10A_POWER_ALGO_H
#include <stdint.h>
#include "ui_conf.h"
#include "coroOS.h"
#include "pm_device.h"
#include "sw6306.h"
/* SW6306 plus auxiliary A-port PA6 and ADC1 DMA telemetry. */
/* Task-facing SW6306 monitor, based on PB02B 100ms staggered LOAD.
 * The displayed BUS voltage/current is the shared SW6306 BUS ADC, not
 * individual per-port current measurements. All getters read RAM only. */
typedef struct {
    uint16_t vbus_mv,ibus_ma,vbat_mv,ibat_ma;
    uint16_t aux_mv, aux_ma;
    int16_t ntc_c10;
    uint8_t capacity_pct,capacity_valid;
    uint8_t aux_enabled, aux_valid, ntc_valid, eta_valid;
    uint8_t initialized,valid,charging,discharging;
    uint8_t c1_on,a1_on,port_busy;
    uint8_t protocol; /* SW6306 QCSTAT protocol code */
    uint32_t sample_ms;
    uint32_t eta_seconds;
} power_monitor_t;
void PowerAlgo_Init(void);
/* SW6306 owner thread: cooperative Init/ADC/Status/Port/Power/Capacity. */
char PowerAlgo_SW6306Thread(coro_pt_t *pt);
void PowerAlgo_RequestBusCycle(void); /* UP: A1 -> C1 -> OFF; owner executes. */
void PowerAlgo_OnSharedIRQ(void); /* ISR hint only; no I2C in EXTI. */
void PowerAlgo_GetMonitor(power_monitor_t *out);
/* Display cache keeps published values while new transfers are pending. */
void PowerAlgo_GetDisplayMonitor(power_monitor_t *out);
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
/* One RAM record and a priority notification mailbox, foreground only. */
enum { POWER_NOTICE_INFO = 1u,
       POWER_NOTICE_WARNING,
       POWER_NOTICE_FAULT };
typedef struct {
    uint8_t code, severity, active, valid;
    uint16_t count;
    sw6306_fault_snapshot_t raw;
} power_fault_view_t;
typedef struct {
    uint8_t code, severity;
} power_notice_t;
void PowerAlgo_GetFaultView(power_fault_view_t *out);
uint8_t PowerAlgo_NoticePending(void);
uint8_t PowerAlgo_TakeNotice(power_notice_t *out);
const char *PowerAlgo_NoticeText(uint8_t code); /* English I18N key */
void PowerAlgo_AddFaultItems(ui_page_t *page);
void PowerAlgo_UpdateFaultItems(ui_t *ui);
void PowerAlgo_ExtLoadReportBlocked(void);
/* External consumers share a background SW6306 meter path. Foreground callers only. */
enum { EXT_LOAD_WLED = 0x01u,
       EXT_LOAD_AUX = 0x02u };
typedef enum { EXT_LOAD_BLOCKED,
               EXT_LOAD_WAIT,
               EXT_LOAD_READY } ext_load_state_t;
#define EXT_LOAD_WAIT_MS 3000u
uint8_t PowerAlgo_ExtLoadRequest(uint8_t load);
void PowerAlgo_ExtLoadSetActive(uint8_t load);
void PowerAlgo_ExtLoadRelease(uint8_t load);
ext_load_state_t PowerAlgo_ExtLoadState(uint8_t load, uint32_t now);
int16_t PowerAlgo_ExtLoadTemperature(void);
uint8_t PowerAlgo_GetExtLoadTemperature(int16_t *c10);
void PowerAlgo_ReportWLEDOverheat(void);

/* ADC_RTC_AuxAThread owns the ADC and SUB_EN. */
typedef enum { AUX_CMD_ENABLE,
               AUX_CMD_DISABLE,
               AUX_CMD_TOGGLE } aux_command_t;
void PowerAlgo_AdcInit(void);
void PowerAlgo_AdcPoll(void);
void PowerAlgo_AuxA_Request(aux_command_t command);
void PowerAlgo_AuxA_SetAutoOffCurrent(unsigned threshold_ma);
void PowerAlgo_AuxA_OnEvents(uint8_t events);
uint8_t PowerAlgo_AuxA_TakeInserted(void);
void PowerAlgo_AddPowerLimitItems(ui_page_t *page);
void PowerAlgo_AddPortItems(ui_page_t *page);
void PowerAlgo_AddProtocolItems(ui_page_t *page, uint8_t group);
void PowerAlgo_AddStatisticsItems(ui_page_t *page);
void PowerAlgo_AddBatteryItems(ui_page_t *page);
void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c10,uint8_t *valid);
#endif
