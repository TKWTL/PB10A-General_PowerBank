/* SW6306 and auxiliary USB-A power policy; preserve PD and live outputs.
 * Only request the IC's low-power register mode when its live port status
 * confirms every connection is idle. Never force charge/discharge OFF. */
#include "power_algo.h"
#include "pm_device.h"
#include "sw6306.h"
#include "adc.h"
#include "main.h"
#include "millis.h"
#include "core/ui.h"
#include "pm_api.h"
#include "system_algo.h"
#include <string.h>

#define POWER_SW_SLEEP_BLOCK 0x08u /* separate from scanner (0x02) and radio (0x04) */
#define SW_REINIT_DELAY_MS 5000u
#define SW_MONITOR_STALE_MS 2500u

enum {
    BATTERY_CHARGE,
    BATTERY_VOLTAGE,
    BATTERY_MAX_ENERGY,
    BATTERY_REMAIN_ENERGY,
    BATTERY_TEMP,
    BATTERY_ITEM_COUNT
};
static int s_battery_values[BATTERY_ITEM_COUNT];
static ui_item_t s_battery_items[BATTERY_ITEM_COUNT];
static ui_data_t s_battery_data[BATTERY_ITEM_COUNT];
static ui_element_t s_battery_elements[BATTERY_ITEM_COUNT];

enum { POWER_BUS_VOLTAGE,
       POWER_BUS_CURRENT,
       POWER_AUX_VOLTAGE,
       POWER_AUX_CURRENT,
       POWER_CHIP_TEMP,
       POWER_ITEM_COUNT };
static int s_power_values[POWER_ITEM_COUNT];
static ui_item_t s_power_items[POWER_ITEM_COUNT];
static ui_data_t s_power_data[POWER_ITEM_COUNT];
static ui_element_t s_power_elements[POWER_ITEM_COUNT];

static power_monitor_t s_monitor;
static uint32_t s_full_mwh, s_remain_mwh;
static uint32_t s_eta_power_mw, s_eta_refresh_at, s_eta_stamp;
static uint8_t s_eta_mode, s_adc_fresh, s_bus_display_valid;
static uint8_t s_sw_phase,s_capacity_div,s_idle_verified,s_status_fresh;
static uint8_t s_sw_owner_busy,s_cycle_ok;
static uint32_t s_sw_seq_before;
static uint32_t s_retry_at;
static uint8_t s_port_baseline_valid,s_previous_connections;
/* Independent request/result mailbox, single owner: PowerAlgo_SW6306Thread. */
static uint8_t s_radio_battery_check_request,s_radio_battery_check_result;
static uint8_t s_radio_battery_adc_ok,s_radio_battery_pct;
static uint8_t s_bg_radio_reads_allowed;
static uint16_t s_radio_battery_mv;

static void sw6306_bus_read_failed(void) {
    s_cycle_ok = s_bus_display_valid = s_monitor.eta_valid = 0u;
}

static void sw6306_display_read_failed(void) {
    sw6306_bus_read_failed();
    s_monitor.ntc_valid = 0u;
    s_power_data[POWER_BUS_VOLTAGE].ptr = NULL;
    s_power_data[POWER_BUS_CURRENT].ptr = NULL;
    s_power_data[POWER_CHIP_TEMP].ptr = NULL;
    for (unsigned i = 0; i < BATTERY_ITEM_COUNT; ++i)
        s_battery_data[i].ptr = NULL;
}

/* An insertion is a 0->1 transition in a successful live port snapshot.
 * The initial snapshot is only a baseline, not a synthetic insert event. */
static void sw6306_connection_update(void) {
    uint8_t connected=SW6306_ConnectedPortMask();
    if(s_port_baseline_valid) {
        uint8_t newly_connected=(uint8_t)(connected &
            (uint8_t)~s_previous_connections);
        if(newly_connected && System_AutoPowerbankNavigationEnabled())
            MiaoUi_RequestNavigate(UI_TARGET_POWERBANK);
    }
    s_previous_connections=connected;
    s_port_baseline_valid=1u;
}

/*************************** SW6306 低功耗回调 ***************************/
static uint8_t s_sw_low_power;
/* Suspend and resume are repeatable PM callbacks, not blocking threads.
 * PM must never overlap an in-progress SW6306 operation owned by coroOS.
 * If a transition is aborted while I2C is active, finish its transfer
 * before reusing the child PT. */
static coro_pt_t s_sw_suspend_pt,s_sw_resume_pt;
static uint8_t s_sw_suspend_stage,s_sw_resume_started;
static uint32_t s_sw_verify_before;

static pm_hook_result_t sw_prepare(void *ctx) {
    (void)ctx;
    return s_sw_owner_busy ? PM_HOOK_PENDING : PM_HOOK_DONE;
}

static pm_hook_result_t sw_suspend(void *ctx) {
    char result;
    (void)ctx;
    if(s_sw_low_power) return PM_HOOK_DONE;
    if(s_sw_owner_busy) return PM_HOOK_PENDING;
    if(!SW6306_IsInitialized()) {
        pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u);
        return PM_HOOK_ERROR;
    }

    if(s_sw_suspend_stage==0u) {
        SW6306_OperationReset();
        s_sw_verify_before=SW6306_PortReadSequence();
        PT_INIT(&s_sw_suspend_pt);
        s_sw_suspend_stage=1u;
    }
    if(s_sw_suspend_stage==1u) {
        result=SW6306_PortStatusLoad(&s_sw_suspend_pt);
        if(PT_SCHEDULE(result)) return PM_HOOK_PENDING;
        s_sw_suspend_stage=0u;
        s_idle_verified=(uint8_t)(SW6306_LastOperationOK() &&
            SW6306_PortReadSequence()!=s_sw_verify_before &&
            SW6306_TransportPortsIdle());
        if(!s_idle_verified) {
            pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u);
            return PM_HOOK_ERROR;
        }
        SW6306_OperationReset();
        PT_INIT(&s_sw_suspend_pt);
        s_sw_suspend_stage=2u;
    }
    if(s_sw_suspend_stage==2u) {
        result=SW6306_LPSet(&s_sw_suspend_pt);
        if(PT_SCHEDULE(result))return PM_HOOK_PENDING;
        s_sw_suspend_stage=0u;
        if(!SW6306_LastOperationOK()) {
            SW6306_Invalidate();
            return PM_HOOK_ERROR;
        }
        s_sw_low_power=1u;
    }
    return PM_HOOK_DONE;
}

static pm_hook_result_t sw_resume(void *ctx) {
    char result;
    (void)ctx;
    if(s_sw_owner_busy)return PM_HOOK_PENDING;

    /* A key wake may abort prepare/suspend while SW I2C is in flight.
     * Drain the previous child to completion; do not start another
     * transaction on the same static child PT before it settles. */
    if(s_sw_suspend_stage==1u) {
        result=SW6306_PortStatusLoad(&s_sw_suspend_pt);
        if(PT_SCHEDULE(result))return PM_HOOK_PENDING;
        s_sw_suspend_stage=0u; /* do NOT start LPSET after cancellation */
    }
    else if(s_sw_suspend_stage==2u) {
        result=SW6306_LPSet(&s_sw_suspend_pt);
        if(PT_SCHEDULE(result))return PM_HOOK_PENDING;
        s_sw_low_power=SW6306_LastOperationOK()?1u:0u;
        s_sw_suspend_stage=0u;
        if(!s_sw_low_power)SW6306_Invalidate();
    }

    if(!s_sw_low_power) {
        s_sw_resume_started=0u;
        return PM_HOOK_DONE;
    }
    if(!s_sw_resume_started) {
        SW6306_OperationReset();
        PT_INIT(&s_sw_resume_pt);
        s_sw_resume_started=1u;
    }
    result=SW6306_Unlock(&s_sw_resume_pt);
    if(PT_SCHEDULE(result))return PM_HOOK_PENDING;
    s_sw_resume_started=0u;
    s_sw_low_power=0u;
    s_idle_verified=0u;
    s_monitor.valid=0u;
    s_monitor.capacity_valid = 0u;
    s_adc_fresh = 0u;
    s_capacity_div = 1u;
    if(!SW6306_LastOperationOK()) {
        sw6306_display_read_failed();
        SW6306_Invalidate();
        return PM_HOOK_ERROR;
    }
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_node={
    "sw6306",sw_prepare,sw_suspend,sw_resume,0,90u,0,0
};
pm_hook_result_t PowerAlgo_BackgroundResume(void) {
    pm_hook_result_t result=sw_resume(0);
    if(result==PM_HOOK_DONE)s_bg_radio_reads_allowed=1u;
    return result;
}
pm_hook_result_t PowerAlgo_BackgroundSuspend(void) {
    /* Stop starting new SW6306 transactions before checking for a
     * completed actor. sw_suspend() itself waits for s_sw_owner_busy. */
    s_bg_radio_reads_allowed=0u;
    return sw_suspend(0);
}
void PowerAlgo_BackgroundAbort(void) {
    s_bg_radio_reads_allowed=0u;
    s_radio_battery_check_request=0u;
    s_radio_battery_check_result=0u;
}


/************************* 附加 A 口与 ADC DMA *************************/
#define AUX_PM_BLOCK 0x40u
#define AUX_DEBOUNCE_MS 40u
#define AUX_STARTUP_MS 10u
#define AUX_FAST_SAMPLE_MS 5u
#define AUX_SHORT_VOLTAGE_MV 2000u
#define AUX_CURRENT_ZERO_MA 10u
#define AUX_AUTO_OFF_MS 60000u
#define AUX_A_VDIV_HIGH 680u
#define AUX_A_VDIV_LOW 220u
#define AUX_A_CURRENT_MV_PER_A 500u

#define ADC_BATCH_MS 500u
#define ADC_SCAN_GAP_MS 25u
#define ADC_AVERAGE_SAMPLES 16u

typedef enum { AUX_STATE_OFF,
               AUX_STATE_STARTUP,
               AUX_STATE_ON } aux_state_t;

static uint16_t s_dma[PB_ADC_SCAN_COUNT] __attribute__((aligned(4)));
static uint32_t s_adc_accumulator[PB_ADC_SCAN_COUNT];
static uint32_t s_adc_batch_at, s_display_sample_at;
static uint8_t s_adc_sample_count, s_active, s_suspended, s_adc_initialized;
static uint8_t s_scan_for_display, s_scan_for_protection;
static uint8_t s_system_adc_valid;
static int s_vdda_mv, s_die_temp_c10;

static aux_state_t s_aux_state;
static aux_command_t s_aux_command;
static uint8_t s_aux_command_pending, s_aux_candidate, s_aux_latched, s_aux_inserted;
static uint32_t s_aux_edge_at, s_aux_sample_at, s_aux_low_since;
static uint8_t s_aux_low_current;
static unsigned s_aux_auto_off_ma = 50u;

static void aux_publish(uint8_t valid, uint16_t mv, uint16_t ma) {
    if (ma < AUX_CURRENT_ZERO_MA)
        ma = 0u;
    s_monitor.aux_valid = valid;
    s_monitor.aux_mv = mv;
    s_monitor.aux_ma = ma;
    s_power_values[POWER_AUX_VOLTAGE] = (int)((mv + 5u) / 10u);
    s_power_values[POWER_AUX_CURRENT] = (int)((ma + 5u) / 10u);
    s_power_data[POWER_AUX_VOLTAGE].ptr = valid ? &s_power_values[POWER_AUX_VOLTAGE] : NULL;
    s_power_data[POWER_AUX_CURRENT].ptr = valid ? &s_power_values[POWER_AUX_CURRENT] : NULL;
}

static void aux_disable(void) {
    LL_GPIO_ResetOutputPin(SUB_EN_GPIO_Port, SUB_EN_Pin);
    s_aux_state = AUX_STATE_OFF;
    s_monitor.aux_enabled = 0u;
    s_aux_low_current = 0u;
    s_scan_for_protection = 0u;
    aux_publish(1u, 0u, 0u);
    pm_api_set_sleep_block(AUX_PM_BLOCK, 0u);
}

static void adc_batch_begin(uint32_t now) {
    s_adc_batch_at = s_display_sample_at = now;
    s_adc_sample_count = 0u;
    s_scan_for_display = 0u;
    memset(s_adc_accumulator, 0, sizeof(s_adc_accumulator));
}

static void aux_enable(void) {
    if (s_aux_state != AUX_STATE_OFF || !s_adc_initialized)
        return;
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port, SUB_EN_Pin);
    uint32_t now = Millis();
    s_aux_sample_at = now + AUX_STARTUP_MS;
    s_aux_state = AUX_STATE_STARTUP;
    s_monitor.aux_enabled = 1u;
    aux_publish(0u, 0u, 0u);
    /* Discard samples taken with the previous output state. */
    adc_batch_begin(now);
    pm_api_set_sleep_block(AUX_PM_BLOCK, 1u);
}

void PowerAlgo_AuxA_Request(aux_command_t command) {
    s_aux_command = command;
    s_aux_command_pending = 1u;
}

void PowerAlgo_AuxA_SetAutoOffCurrent(unsigned threshold_ma) {
    s_aux_auto_off_ma = threshold_ma;
    s_aux_low_current = 0u;
}

void PowerAlgo_AuxA_OnEvents(uint8_t events) {
    if ((events & PM_WAKE_SUB_INDET) && !s_aux_latched) {
        s_aux_edge_at = Millis();
        s_aux_candidate = 1u;
    }
}

uint8_t PowerAlgo_AuxA_TakeInserted(void) {
    uint8_t inserted = s_aux_inserted;
    s_aux_inserted = 0u;
    return inserted;
}

static void aux_service(uint32_t now) {
    if (LL_GPIO_IsInputPinSet(SUB_INDET_GPIO_Port, SUB_INDET_Pin)) {
        s_aux_candidate = 0u;
        s_aux_latched = 0u;
    }
    if (s_suspended || pm_api_ui_should_block())
        return;

    uint8_t inserted = s_aux_candidate && (uint32_t)(now - s_aux_edge_at) >= AUX_DEBOUNCE_MS;
    if (inserted) {
        s_aux_candidate = 0u;
        s_aux_latched = 1u;
        s_aux_inserted = 1u;
    }

    if (s_aux_command_pending) {
        s_aux_command_pending = 0u;
        if (s_aux_command == AUX_CMD_DISABLE)
            aux_disable();
        else
            aux_enable();
    } else if (inserted) {
        aux_enable();
    }
}

static pm_hook_result_t power_adc_suspend(void *ctx) {
    (void)ctx;
    aux_disable();
    s_aux_command_pending = 0u;
    s_suspended = 1u;
    s_active = 0u;
    s_system_adc_valid = 0u;
    return ADC_ScanStop() ? PM_HOOK_DONE : PM_HOOK_ERROR;
}

static pm_hook_result_t power_adc_resume(void *ctx) {
    (void)ctx;
    if (s_suspended) {
        s_suspended = 0u;
        adc_batch_begin(Millis());
    }
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_power_adc_pm = {
    "power-adc", 0, power_adc_suspend, power_adc_resume, 0, 200u, 0, 0};

void PowerAlgo_AdcInit(void) {
    s_adc_initialized = ADC_Calibrate();
    adc_batch_begin(Millis());
}

static uint32_t adc_vdda(const uint16_t *raw) {
    if (raw[PB_ADC_VREF_SLOT] == 0u)
        return 0u;
    uint32_t mv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(raw[PB_ADC_VREF_SLOT], LL_ADC_RESOLUTION_12B);
    return mv <= UINT16_MAX ? mv : 0u;
}

static uint8_t aux_measure(const uint16_t *raw, uint32_t vdda, uint16_t *mv, uint16_t *ma) {
    uint32_t pin_mv = ((uint32_t)raw[PB_ADC_AUX_VOLTAGE_SLOT] * vdda + 2047u) / 4095u;
    uint32_t voltage = (pin_mv * (AUX_A_VDIV_HIGH + AUX_A_VDIV_LOW) + AUX_A_VDIV_LOW / 2u) / AUX_A_VDIV_LOW;
    uint32_t current = ((uint32_t)raw[PB_ADC_AUX_CURRENT_SLOT] * vdda * (1000u / AUX_A_CURRENT_MV_PER_A) + 2047u) / 4095u;
    if (voltage > UINT16_MAX || current > UINT16_MAX)
        return 0u;
    *mv = (uint16_t)voltage;
    *ma = (uint16_t)current;
    return 1u;
}

static void aux_invalid_sample(void) {
    s_aux_low_current = 0u;
    aux_publish(0u, 0u, 0u);
}

static void aux_protect(uint32_t now, uint32_t vdda) {
    uint16_t mv, ma;
    if (!aux_measure(s_dma, vdda, &mv, &ma)) {
        aux_invalid_sample();
        return;
    }
    if (mv <= AUX_SHORT_VOLTAGE_MV) {
        aux_disable();
        return;
    }
    s_aux_state = AUX_STATE_ON;
    if (ma >= s_aux_auto_off_ma) {
        s_aux_low_current = 0u;
    } else if (!s_aux_low_current) {
        s_aux_low_current = 1u;
        s_aux_low_since = now;
    } else if ((uint32_t)(now - s_aux_low_since) >= AUX_AUTO_OFF_MS) {
        aux_disable();
    }
}

static void adc_accumulate_display(void) {
    uint16_t avg[PB_ADC_SCAN_COUNT];
    for (unsigned i = 0; i < PB_ADC_SCAN_COUNT; ++i)
        s_adc_accumulator[i] += s_dma[i];
    if (++s_adc_sample_count < ADC_AVERAGE_SAMPLES)
        return;
    for (unsigned i = 0; i < PB_ADC_SCAN_COUNT; ++i)
        avg[i] = (uint16_t)((s_adc_accumulator[i] + ADC_AVERAGE_SAMPLES / 2u) / ADC_AVERAGE_SAMPLES);

    uint32_t vdda = adc_vdda(avg);
    if (s_aux_state == AUX_STATE_ON) {
        uint16_t mv, ma;
        if (vdda && aux_measure(avg, vdda, &mv, &ma))
            aux_publish(1u, mv, ma);
        else
            aux_publish(0u, 0u, 0u);
    }

    /* Retain tenths of a degree using the factory TS_CAL1/TS_CAL2 values. */
    int32_t cal_span = (int32_t)*TEMPSENSOR_CAL2_ADDR - (int32_t)*TEMPSENSOR_CAL1_ADDR;
    s_system_adc_valid = (uint8_t)(vdda != 0u && cal_span != 0);
    if (s_system_adc_valid) {
        int32_t scaled_temp10 = (int32_t)(((uint32_t)avg[PB_ADC_TEMPERATURE_SLOT] * vdda * 10u +
                                           TEMPSENSOR_CAL_VREFANALOG / 2u) /
                                          TEMPSENSOR_CAL_VREFANALOG);
        s_die_temp_c10 = (int)(TEMPSENSOR_CAL1_TEMP * 10 +
                               (scaled_temp10 - (int32_t)*TEMPSENSOR_CAL1_ADDR * 10) *
                                   (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) / cal_span);
        s_vdda_mv = (int)vdda;
    }
}

static void adc_scan_failed(void) {
    if (s_scan_for_protection)
        aux_invalid_sample();
    if (s_scan_for_display) {
        s_adc_sample_count = ADC_AVERAGE_SAMPLES; /* abandon this batch */
        s_system_adc_valid = 0u;
    }
    (void)ADC_ScanStop();
}

void PowerAlgo_AdcPoll(void) {
    aux_service(Millis());
    if (s_suspended || !s_adc_initialized)
        return;

    uint32_t now = Millis();
    if (s_active) {
        ADC_ScanStatus result = ADC_ScanState();
        if (result == ADC_SCAN_BUSY)
            return;
        s_active = 0u;
        uint32_t vdda = result == ADC_SCAN_DONE ? adc_vdda(s_dma) : 0u;
        if (!vdda) {
            adc_scan_failed();
        } else {
            if (s_scan_for_protection)
                aux_protect(now, vdda);
            if (s_scan_for_display)
                adc_accumulate_display();
        }
    }

    if ((uint32_t)(now - s_adc_batch_at) >= ADC_BATCH_MS)
        adc_batch_begin(now);

    s_scan_for_display = s_adc_sample_count < ADC_AVERAGE_SAMPLES &&
                         (int32_t)(now - s_display_sample_at) >= 0;
    s_scan_for_protection = s_aux_state != AUX_STATE_OFF &&
                            (int32_t)(now - s_aux_sample_at) >= 0;
    /* When AUX is active, use the same scan for both paths. The first
     * protection scan starts only after the startup deadline. */
    if (!(s_aux_state == AUX_STATE_OFF ? s_scan_for_display : s_scan_for_protection))
        return;
    if (s_scan_for_protection)
        s_aux_sample_at = now + AUX_FAST_SAMPLE_MS;
    if (s_scan_for_display)
        s_display_sample_at = now + ADC_SCAN_GAP_MS;
    s_active = ADC_ScanStart(s_dma);
    if (!s_active)
        adc_scan_failed();
}

void PowerAlgo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[POWER_ITEM_COUNT] = {" BUS V", " BUS A", " Aux-A V", " Aux-A A", " Chip Temp °C"};
    for (unsigned i = 0; i < POWER_ITEM_COUNT; ++i) {
        s_power_data[i].name = names[i];
        s_power_data[i].ptr = (i == POWER_AUX_VOLTAGE || i == POWER_AUX_CURRENT) ? &s_power_values[i] : NULL;
        s_power_data[i].dataType = UI_DATA_FLOAT;
        s_power_data[i].actionType = UI_DATA_ACTION_RO;
        s_power_data[i].decimals = i == POWER_CHIP_TEMP ? 1u : 2u;
        s_power_elements[i].data = &s_power_data[i];
        Create_element(&s_power_items[i], &s_power_elements[i]);
        AddItem(names[i], UI_ITEM_DATA, 0, &s_power_items[i], page, 0, 0);
    }
}

void PowerAlgo_AddBatteryItems(ui_page_t *page) {
    static const char *const names[BATTERY_ITEM_COUNT] = {
        " Charge %", " Battery V", " Max Wh", " Remain Wh", " Batt Temp °C"};
    for (unsigned i = 0; i < BATTERY_ITEM_COUNT; ++i) {
        s_battery_data[i].name = names[i];
        s_battery_data[i].dataType = i == BATTERY_CHARGE ? UI_DATA_INT : UI_DATA_FLOAT;
        s_battery_data[i].actionType = UI_DATA_ACTION_RO;
        s_battery_elements[i].data = &s_battery_data[i];
        Create_element(&s_battery_items[i], &s_battery_elements[i]);
        AddItem(names[i], UI_ITEM_DATA, 0, &s_battery_items[i], page, 0, 0);
    }
    s_battery_data[BATTERY_VOLTAGE].decimals = 2u;
    s_battery_data[BATTERY_TEMP].decimals = 1u;
}

static void sw6306_cache_adc(void) {
    uint8_t valid = SW6306_LastOperationOK();
    if (valid && !SW6306_IsInitialized())
        return;
    s_power_data[POWER_BUS_VOLTAGE].ptr = valid ? &s_power_values[POWER_BUS_VOLTAGE] : NULL;
    s_power_data[POWER_BUS_CURRENT].ptr = valid ? &s_power_values[POWER_BUS_CURRENT] : NULL;
    s_power_data[POWER_CHIP_TEMP].ptr = valid ? &s_power_values[POWER_CHIP_TEMP] : NULL;
    s_battery_data[BATTERY_VOLTAGE].ptr = valid ? &s_battery_values[BATTERY_VOLTAGE] : NULL;
    if (!valid) {
        sw6306_bus_read_failed();
        s_monitor.ntc_valid = 0u;
        s_battery_data[BATTERY_TEMP].ptr = NULL;
        return;
    }
#if SW6306_USE_FLOAT_API
    s_power_values[POWER_CHIP_TEMP] = (int)(SW6306_ReadTCHIP() * 10.0f);
#else
    s_power_values[POWER_CHIP_TEMP] = SW6306_ReadTCHIP_c10();
#endif

    s_power_values[POWER_BUS_VOLTAGE] = (SW6306_ReadVBUS() + 5u) / 10u;
    s_power_values[POWER_BUS_CURRENT] = (SW6306_ReadIBUS() + 5u) / 10u;
    s_battery_values[BATTERY_VOLTAGE] = (SW6306_ReadVBAT() + 5u) / 10u;
}

static void sw6306_cache_ntc(void) {
    if (!s_status_fresh) {
        sw6306_bus_read_failed();
        s_monitor.ntc_valid = 0u;
        s_battery_data[BATTERY_TEMP].ptr = NULL;
        return;
    }
#if SW6306_NTC_TEMP_CALC_EN
    if (!s_adc_fresh || !SW6306_IsInitialized())
        return;
#if SW6306_USE_FLOAT_API
    float c = SW6306_CalcNTCTemp_C();
    if (!(c >= -40.0f && c <= 125.0f))
        return;
    int16_t c10 = (int16_t)(c * 10.0f);
#else
    int16_t c10 = SW6306_CalcNTCTemp_c10();
    if (c10 == SW6306_NTC_INVALID_C10)
        return;
#endif
    s_monitor.ntc_c10 = c10;
    s_monitor.ntc_valid = 1u;
    s_battery_values[BATTERY_TEMP] = c10;
    s_battery_data[BATTERY_TEMP].ptr = &s_battery_values[BATTERY_TEMP];
#endif
}

static void sw6306_cache_battery_energy(unsigned item, uint32_t mwh) {
    uint32_t dwh = (mwh + 50u) / 100u;
    /* The value column fits five digits or characters. */
    s_battery_data[item].decimals = dwh < 10000u ? 1u : 0u;
    s_battery_values[item] = (int)(s_battery_data[item].decimals ? dwh : (mwh + 500u) / 1000u);
    s_battery_data[item].ptr = &s_battery_values[item];
}

static uint8_t sw6306_cache_battery_capacity(void) {
    if (!SW6306_LastOperationOK() || !SW6306_CapacityLoadOK()) {
        s_battery_data[BATTERY_CHARGE].ptr = NULL;
        s_battery_data[BATTERY_MAX_ENERGY].ptr = NULL;
        s_battery_data[BATTERY_REMAIN_ENERGY].ptr = NULL;
        s_monitor.eta_valid = 0u;
        return 0u;
    }

    if (!SW6306_IsInitialized())
        return 0u;

    uint8_t pct = SW6306_ReadCapacity();
    s_battery_values[BATTERY_CHARGE] = pct <= 100u ? pct : 100u;
    s_battery_data[BATTERY_CHARGE].ptr = &s_battery_values[BATTERY_CHARGE];
    s_full_mwh = SW6306_ReadMaxEnergyRounded_mWh();
    s_remain_mwh = SW6306_ReadRemainEnergyRounded_mWh();
    sw6306_cache_battery_energy(BATTERY_MAX_ENERGY, s_full_mwh);
    sw6306_cache_battery_energy(BATTERY_REMAIN_ENERGY, s_remain_mwh);
    return 1u;
}

static void sw6306_update_eta(uint32_t now) {
    uint8_t mode = s_monitor.charging ? 1u : s_monitor.discharging ? 2u
                                                                   : 0u;
    uint32_t power_mw = ((uint32_t)s_monitor.vbat_mv * s_monitor.ibat_ma + 500u) / 1000u;
    if (!s_monitor.valid || !mode || power_mw < 200u) {
        s_eta_mode = 0u;
        s_monitor.eta_valid = 0u;
        return;
    }
    if (mode != s_eta_mode) {
        s_eta_mode = mode;
        s_eta_power_mw = power_mw;
        s_eta_refresh_at = now + 5000u;
        s_monitor.eta_valid = 0u;
        return;
    }

    int32_t diff = (int32_t)power_mw - (int32_t)s_eta_power_mw;
    s_eta_power_mw = (uint32_t)((int32_t)s_eta_power_mw + (diff + (diff < 0 ? -7 : 7)) / 8);
    if (!s_monitor.capacity_valid) {
        s_monitor.eta_valid = 0u;
        return;
    }
    if ((int32_t)(now - s_eta_refresh_at) < 0)
        return;
    s_eta_refresh_at = now + 10000u;
    s_monitor.eta_valid = 0u;
    if (s_full_mwh <= 500u || s_remain_mwh > s_full_mwh + s_full_mwh / 20u ||
        (mode == 1u && s_remain_mwh > s_full_mwh))
        return;

    uint32_t energy = mode == 1u ? s_full_mwh - s_remain_mwh : s_remain_mwh;
    uint32_t hours = energy / s_eta_power_mw;
    if (hours >= 100u)
        return;
    /* Split base-60 conversion to keep the products in 32 bits. */
    uint32_t minutes = (energy % s_eta_power_mw) * 60u;
    uint32_t seconds = hours * 3600u + (minutes / s_eta_power_mw) * 60u +
                       ((minutes % s_eta_power_mw) * 60u + s_eta_power_mw / 2u) / s_eta_power_mw;
    if (seconds < 360000u) {
        s_monitor.eta_seconds = seconds;
        s_monitor.eta_valid = 1u;
        s_eta_stamp = now;
    }
}

/************************ SW6306 定期采样与缓存 ************************/
/* One actor owns the SW6306 PT call tree. Every I2C operation now
 * submits through HAL IRQ and yields only this coroutine while waiting.
 * Allow already-started transfers to finish during UI_OFF preparation;
 * never start new background transactions until RUN is restored. */
char PowerAlgo_SW6306Thread(coro_pt_t *pt) {
    THRD_BEGIN;
    for(;;) {
        if(s_sw_low_power ||
            (pm_api_ui_should_block() && !s_bg_radio_reads_allowed)) {
            THRD_DELAY(20u);
            continue;
        }
        if(!SW6306_IsInitialized()) {
            s_adc_fresh = 0u;
            s_monitor.initialized=0u;
            s_monitor.valid=0u;
            s_monitor.capacity_valid=0u;
            s_idle_verified=0u;
            pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u);
            if((int32_t)(Millis()-s_retry_at)<0) {
                THRD_DELAY(100u);
                continue;
            }
            s_retry_at=Millis()+SW_REINIT_DELAY_MS;
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_Init);
            s_sw_owner_busy=0u;
            if(!SW6306_IsInitialized() || !SW6306_LastOperationOK()) {
                if (!SW6306_LastOperationOK())
                    sw6306_display_read_failed();
                SW6306_Invalidate();
                THRD_DELAY(100u);
                continue;
            }
            s_sw_low_power=0u;
            s_sw_phase=0u;
            s_capacity_div=1u;
            s_status_fresh=0u;
            s_cycle_ok=1u;
            s_monitor.initialized=1u;
            s_port_baseline_valid=0u; /* first successful PortStatusLoad */
            THRD_DELAY(100u);
            continue;
        }
        if(s_radio_battery_check_request) {
            /* Complete a fresh ADC and capacity transaction pair in the
             * same SW6306 owner coroutine after the normal PM resume.
             * Do not treat a cached/failed snapshot as a valid 0% pack. */
            s_radio_battery_check_request=0u;
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_ADCLoad);
            s_sw_owner_busy=0u;
            sw6306_cache_adc();
            s_radio_battery_adc_ok=SW6306_LastOperationOK();
            if(s_radio_battery_adc_ok)
                s_radio_battery_mv=SW6306_ReadVBAT();
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_CapacityLoad);
            s_sw_owner_busy=0u;
            if (sw6306_cache_battery_capacity() && s_radio_battery_adc_ok) {
                s_radio_battery_pct = (uint8_t)s_battery_values[BATTERY_CHARGE];
                s_monitor.capacity_pct=s_radio_battery_pct;
                s_monitor.capacity_valid=1u;
                s_radio_battery_check_result=1u;
            } else
                s_radio_battery_check_result = 2u;
            THRD_DELAY(100u);
            continue;
        }

        if(s_bg_radio_reads_allowed) {
            /* No regular 5-phase scanner in UI_OFF. The inspection
             * explicitly requests a fresh ADC + Capacity pair above. */
            THRD_DELAY(20u);
            continue;
        }
        if(s_sw_phase==0u) {
            s_cycle_ok=1u;
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_ADCLoad);
            s_sw_owner_busy=0u;
            sw6306_cache_adc();
            s_adc_fresh = SW6306_LastOperationOK() && SW6306_IsInitialized();
        }
        else if(s_sw_phase==1u) {
            s_sw_seq_before=SW6306_StatusReadSequence();
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_StatusLoad);
            s_sw_owner_busy=0u;
            s_status_fresh=(uint8_t)(SW6306_LastOperationOK() &&
                SW6306_StatusReadSequence()!=s_sw_seq_before);
            sw6306_cache_ntc();
        }
        else if(s_sw_phase==2u) {
            s_sw_seq_before=SW6306_PortReadSequence();
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_PortStatusLoad);
            s_sw_owner_busy=0u;
            if(SW6306_LastOperationOK() &&
                SW6306_PortReadSequence()!=s_sw_seq_before)
                sw6306_connection_update();
            s_idle_verified=(uint8_t)(SW6306_LastOperationOK() &&
                SW6306_PortReadSequence()!=s_sw_seq_before &&
                SW6306_TransportPortsIdle());
            s_monitor.port_busy=(uint8_t)!s_idle_verified;
            if (!SW6306_LastOperationOK())
                sw6306_bus_read_failed();
            pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,!s_idle_verified);
            if(!s_idle_verified)pm_api_refresh_idle();
        }
        else if(s_sw_phase==4u) {
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_PowerLoad);
            s_sw_owner_busy=0u;
            if (!SW6306_LastOperationOK())
                sw6306_bus_read_failed();
            if(++s_capacity_div>=2u) {
                s_capacity_div=0u;
                s_sw_owner_busy=1u;
                SW6306_OperationReset();
                THRD_SPAWN_NOARG(SW6306_CapacityLoad);
                s_sw_owner_busy=0u;
                s_monitor.capacity_valid = sw6306_cache_battery_capacity();
                if (s_monitor.capacity_valid)
                    s_monitor.capacity_pct = (uint8_t)s_battery_values[BATTERY_CHARGE];
            }
            s_monitor.initialized = SW6306_IsInitialized();
            s_monitor.valid = s_monitor.initialized && s_status_fresh && s_cycle_ok;
            if (s_monitor.valid) {
                s_bus_display_valid = 1u;
                s_monitor.vbus_mv = SW6306_ReadVBUS();
                s_monitor.ibus_ma = SW6306_ReadIBUS();
                s_monitor.vbat_mv = SW6306_ReadVBAT();
                s_monitor.ibat_ma = SW6306_ReadIBAT();
                s_monitor.c1_on = SW6306_IsPortC1ON();
                s_monitor.a1_on = SW6306_IsPortA1ON();
                s_monitor.charging = SW6306_IsCharging();
                s_monitor.discharging = SW6306_IsDischarging();
                s_monitor.protocol = SW6306_ReadProtocol();
                s_monitor.sample_ms = Millis();
                sw6306_update_eta(s_monitor.sample_ms);
            }
            s_status_fresh=0u;
        }
        /* Phase 3 intentionally leaves an idle gap on the shared bus. */
        s_sw_phase=(uint8_t)((s_sw_phase+1u)%5u);
        THRD_DELAY(100u);
    }
    THRD_END;
}

static void sw6306_count_down_eta(power_monitor_t *out, uint32_t now) {
    if (out->eta_valid) {
        uint32_t elapsed = (uint32_t)(now - s_eta_stamp) / 1000u;
        out->eta_seconds = elapsed < out->eta_seconds ? out->eta_seconds - elapsed : 0u;
    }
}

void PowerAlgo_GetMonitor(power_monitor_t *out) {
    if (!out)
        return;
    *out = s_monitor; /* foreground coroOS only; no interrupt writer */
    if (!SW6306_IsInitialized())
        out->initialized = out->valid = 0u;
    uint32_t now = Millis();
    if ((uint32_t)(now - out->sample_ms) > SW_MONITOR_STALE_MS)
        out->valid = 0u;
    if (!out->valid)
        out->eta_valid = out->ntc_valid = 0u;
    sw6306_count_down_eta(out, now);
}

void PowerAlgo_GetDisplayMonitor(power_monitor_t *out) {
    if (!out)
        return;
    *out = s_monitor;
    out->valid = s_bus_display_valid;
    out->capacity_valid = s_battery_data[BATTERY_CHARGE].ptr != NULL;
    out->capacity_pct = (uint8_t)s_battery_values[BATTERY_CHARGE];
    sw6306_count_down_eta(out, Millis());
}

void PowerAlgo_RequestRadioBatteryCheck(void) {
    s_radio_battery_check_request=1u;
    s_radio_battery_check_result=0u;
}
uint8_t PowerAlgo_TakeRadioBatteryCheck(uint16_t *vbat_mv,uint8_t *capacity_pct) {
    uint8_t result=s_radio_battery_check_result;
    if(result!=0u) {
        if(vbat_mv)*vbat_mv=s_radio_battery_mv;
        if(capacity_pct)*capacity_pct=s_radio_battery_pct;
        s_radio_battery_check_result=0u;
    }
    return result;
}

/**************************** 服务注册与初始化 ***************************/
void PowerAlgo_Init(void) {
    memset(&s_monitor,0,sizeof(s_monitor));
    aux_disable();
    s_monitor.protocol="--";
    s_sw_phase=0u;s_capacity_div=1u;s_idle_verified=0u;
    s_adc_fresh = s_eta_mode = s_bus_display_valid = 0u;
    s_port_baseline_valid=0u;s_previous_connections=0u;
    s_status_fresh=0u;
    s_retry_at=Millis(); /* first service tick attempts SW6306_Init */
    s_sw_low_power=0u;s_sw_owner_busy=0u;
    s_sw_suspend_stage=0u;s_sw_resume_started=0u;
    s_cycle_ok=0u;
    s_radio_battery_check_request=0u;
    s_radio_battery_check_result=0u;
    s_radio_battery_adc_ok=0u;
    s_bg_radio_reads_allowed=0u;
    pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u); /* fail closed until idle proven */
    (void)pm_device_register(&s_node);         /* SW6306 / order 90 */
    (void)pm_device_register(&s_power_adc_pm); /* SUB_EN + ADC / order 200 */
}

void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c10,uint8_t *valid) {
    if(vdda_mv)*vdda_mv=s_vdda_mv;
    if(die_temp_c10)*die_temp_c10=s_die_temp_c10;
    if(valid)*valid=(uint8_t)(s_system_adc_valid && !s_suspended);
}
