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
#include "wled_algo.h"
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

/************************* Powerbank RAM settings *************************/
typedef enum {
    CFG_OUTPUT,
    CFG_INPUT,
    CFG_C1_INPUT,
    CFG_C1_OUTPUT,
    CFG_A1_OUTPUT,
    CFG_IDLE_TIMEOUT,
    CFG_IDLE_THRESHOLD,
    CFG_AUX_ENABLE,
    CFG_AUX_THRESHOLD,
    CFG_PD_OUT,
    CFG_PD_IN,
    CFG_PD_SCP,
    CFG_PD_VOOC,
    CFG_PD_UFCS,
    CFG_PPS_BROADCAST,
    CFG_PPS_11,
    CFG_PPS_21,
    CFG_UFCS_BROADCAST,
    CFG_UFCS_OUT,
    CFG_UFCS_IN,
    CFG_QC,
    CFG_FCP,
    CFG_AFC_OUT,
    CFG_AFC_IN,
    CFG_HVSCP,
    CFG_LVSCP,
    CFG_SCP_IN,
    CFG_PE,
    CFG_SFCP,
    CFG_VOOC_OUT,
    CFG_VOOC_IN,
    CFG_SVOOC,
    CFG_COUNT
} power_setting_t;

#define CFG_BIT(i) (UINT32_C(1) << (i))
#define CFG_POWER_MASK (CFG_BIT(CFG_OUTPUT) | CFG_BIT(CFG_INPUT))
#define CFG_BROADCAST_MASK (CFG_BIT(CFG_PPS_BROADCAST) | CFG_BIT(CFG_UFCS_BROADCAST))
#define CFG_YIELD_MASK (CFG_BIT(CFG_PD_SCP) | CFG_BIT(CFG_PD_VOOC) | CFG_BIT(CFG_PD_UFCS))
#define CFG_SCP_MASK (CFG_BIT(CFG_HVSCP) | CFG_BIT(CFG_LVSCP))
#define CFG_PORT_MASK (CFG_BIT(CFG_C1_INPUT) | CFG_BIT(CFG_C1_OUTPUT) | CFG_BIT(CFG_A1_OUTPUT))
#define CFG_PROTOCOL_MASK ((UINT32_MAX << CFG_PD_OUT) & ~CFG_BROADCAST_MASK)
#define CFG_CHIP_SWITCH_MASK (CFG_PORT_MASK | CFG_PROTOCOL_MASK)
_Static_assert(CFG_COUNT == 32, "settings command mask covers one bit per item");

typedef enum {
    SET_NUMBER,
    SET_ENUM,
    SET_AUX_SWITCH,
    SET_PORT,
    SET_PROTOCOL,
    SET_PD_YIELD,
    SET_PPS,
    SET_SCP,
    SET_BROADCAST
} setting_kind_t;

typedef struct {
    const char *name;
    uint8_t kind, arg0, arg1;
} setting_desc_t;

#define PROTO(name, proto, dir) {name, SET_PROTOCOL, SW6306_PROTO_##proto, SW6306_PROTO_DIR_##dir}
static const setting_desc_t s_settings[CFG_COUNT] = {
    [CFG_OUTPUT] = {" Output W", SET_NUMBER},
    [CFG_INPUT] = {" Input W", SET_NUMBER},
    [CFG_C1_INPUT] = {" C1 Input Fast", SET_PORT, SW6306_PORT_C1, SW6306_PORT_DIR_IN},
    [CFG_C1_OUTPUT] = {" C1 Output Fast", SET_PORT, SW6306_PORT_C1, SW6306_PORT_DIR_OUT},
    [CFG_A1_OUTPUT] = {" A1 Output Fast", SET_PORT, SW6306_PORT_A1, SW6306_PORT_DIR_OUT},
    [CFG_IDLE_TIMEOUT] = {" Idle Timeout", SET_ENUM},
    [CFG_IDLE_THRESHOLD] = {" Idle Threshold", SET_NUMBER},
    [CFG_AUX_ENABLE] = {" Aux-A Enable", SET_AUX_SWITCH},
    [CFG_AUX_THRESHOLD] = {" Aux Idle Current", SET_NUMBER},
    [CFG_PD_OUT] = PROTO(" PD out", PD, SOURCE),
    [CFG_PD_IN] = PROTO(" PD in", PD, SINK),
    [CFG_PD_SCP] = {" PD to SCP", SET_PD_YIELD},
    [CFG_PD_VOOC] = {" PD to VOOC", SET_PD_YIELD},
    [CFG_PD_UFCS] = {" PD to UFCS", SET_PD_YIELD},
    [CFG_PPS_BROADCAST] = {" PPS Broadcast", SET_BROADCAST},
    [CFG_PPS_11] = {" 11V PPS", SET_PPS, SW6306_PPS_1},
    [CFG_PPS_21] = {" 21V PPS", SET_PPS, SW6306_PPS_3},
    [CFG_UFCS_BROADCAST] = {" UFCS Broadcast", SET_BROADCAST},
    [CFG_UFCS_OUT] = PROTO(" UFCS out", UFCS, SOURCE),
    [CFG_UFCS_IN] = PROTO(" UFCS in", UFCS, SINK),
    [CFG_QC] = PROTO(" QC", QC, SOURCE),
    [CFG_FCP] = PROTO(" FCP", FCP, SOURCE),
    [CFG_AFC_OUT] = PROTO(" AFC out", AFC, SOURCE),
    [CFG_AFC_IN] = PROTO(" AFC in", AFC, SINK),
    [CFG_HVSCP] = {" HVSCP out", SET_SCP},
    [CFG_LVSCP] = {" LVSCP out", SET_SCP},
    [CFG_SCP_IN] = PROTO(" SCP in", SCP, SINK),
    [CFG_PE] = PROTO(" PE", PE, SOURCE),
    [CFG_SFCP] = PROTO(" SFCP", SFCP, SOURCE),
    [CFG_VOOC_OUT] = PROTO(" VOOC out", VOOC, SOURCE),
    [CFG_VOOC_IN] = PROTO(" VOOC in", VOOC, SINK),
    [CFG_SVOOC] = PROTO(" SVOOC", SVOOC, SOURCE),
};
#undef PROTO

static const unsigned s_idle_seconds[] = {5u, 10u, 30u, 60u, 300u, 900u, 1800u};
static const char *const s_idle_labels[] = {"5s", "10s", "30s", "1min", "5min", "15min", "30min", "Off"};
static int s_output_w = SW6306_OUTPUT_POWER_MAX, s_input_w = SW6306_INPUT_POWER_MAX;
static uint8_t s_output_requested = SW6306_OUTPUT_POWER_MAX, s_input_requested = SW6306_INPUT_POWER_MAX;
static int s_idle_timeout = 7, s_idle_threshold = 12, s_aux_threshold = 50;
static uint8_t s_setting_switch[CFG_COUNT] = {[CFG_AUX_ENABLE] = 1u};
#define s_aux_allowed (s_setting_switch[CFG_AUX_ENABLE])

static ui_item_t s_setting_items[CFG_COUNT];
static ui_element_t s_setting_elements[CFG_COUNT - 2u];
static ui_data_t s_setting_data[CFG_COUNT - 2u];
static unsigned s_setting_data_count;
static uint32_t s_config_dirty;
static uint8_t s_idle_running, s_idle_fired;
static uint32_t s_idle_since;
static uint8_t s_bus_force_off;

static void idle_reset(void) {
    s_idle_running = s_idle_fired = 0u;
}

static void setting_changed(ui_t *ui) {
    unsigned index = (unsigned)(ui->nowItem - s_setting_items);
    if (index == CFG_AUX_THRESHOLD) {
        PowerAlgo_AuxA_SetAutoOffCurrent((unsigned)s_aux_threshold);
    } else if (index == CFG_IDLE_TIMEOUT || index == CFG_IDLE_THRESHOLD) {
        idle_reset();
    } else {
        if (index == CFG_OUTPUT)
            s_output_requested = (uint8_t)s_output_w;
        else if (index == CFG_INPUT)
            s_input_requested = (uint8_t)s_input_w;
        s_config_dirty |= CFG_BIT(index);
    }
}

static void add_settings(ui_page_t *page, unsigned first, unsigned end) {
    for (unsigned i = first; i < end; ++i) {
        const setting_desc_t *desc = &s_settings[i];
        if (desc->kind == SET_BROADCAST) {
            AddItem(desc->name, UI_ITEM_ONCE_FUNCTION, 0, &s_setting_items[i], page, 0, setting_changed);
            continue;
        }

        unsigned slot = s_setting_data_count++;
        ui_data_t *data = &s_setting_data[slot];
        data->name = desc->name;
        data->actionType = UI_DATA_ACTION_RW;
        data->function = i == CFG_AUX_ENABLE ? NULL : setting_changed;
        data->functionType = UI_DATA_FUNCTION_EXIT_EXECUTE;
        if (desc->kind == SET_NUMBER || desc->kind == SET_ENUM) {
            data->dataType = UI_DATA_INT;
            switch (i) {
            case CFG_OUTPUT:
            case CFG_INPUT:
                data->ptr = i == CFG_OUTPUT ? &s_output_w : &s_input_w;
                data->min = 5;
                data->max = i == CFG_OUTPUT ? SW6306_OUTPUT_POWER_MAX : SW6306_INPUT_POWER_MAX;
                data->step = 5;
                break;
            case CFG_IDLE_TIMEOUT:
                data->name = "Timeout";
                data->ptr = &s_idle_timeout;
                data->dataType = UI_DATA_ENUM;
                data->options = s_idle_labels;
                data->max = 7;
                data->step = 1;
                break;
            case CFG_IDLE_THRESHOLD:
                data->name = "Idle mA";
                data->ptr = &s_idle_threshold;
                data->max = 100;
                data->step = 4;
                break;
            case CFG_AUX_THRESHOLD:
                data->name = "Aux Idle mA";
                data->ptr = &s_aux_threshold;
                data->min = 10;
                data->max = 100;
                data->step = 10;
                break;
            default:
                break;
            }
        } else {
            data->dataType = UI_DATA_SWITCH;
            data->ptr = &s_setting_switch[i];
            s_setting_switch[i] = 1u;
        }
        if (i == CFG_IDLE_THRESHOLD || i == CFG_IDLE_TIMEOUT || i == CFG_AUX_THRESHOLD)
            data->functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
        s_setting_elements[slot].data = data;
        Create_element(&s_setting_items[i], &s_setting_elements[slot]);
        AddItem(desc->name, UI_ITEM_DATA, 0, &s_setting_items[i], page, 0, 0);
    }
}

void PowerAlgo_AddPowerLimitItems(ui_page_t *page) {
    add_settings(page, CFG_OUTPUT, CFG_C1_INPUT);
}

void PowerAlgo_AddPortItems(ui_page_t *page) {
    add_settings(page, CFG_C1_INPUT, CFG_PD_OUT);
}

void PowerAlgo_AddProtocolItems(ui_page_t *page, uint8_t group) {
    if (group == 1u)
        add_settings(page, CFG_PD_OUT, CFG_QC);
    else
        add_settings(page, CFG_QC, CFG_COUNT);
}


/************************ WLED / Aux-A common policy ************************/
#define EXT_LOAD_PM_BLOCK 0x40u
#define EXT_BATTERY_MIN_MV 14000u /* Five cells x configured 2.8 V UVLO. */
#define EXT_NTC_STOP_C10 600
#define EXT_NTC_RESTART_C10 450
#define METER_RELEASE_MS 200u
#define METER_HANDOVER_MS 1000u
#define METER_RETRY_MS 1000u

typedef enum { EXT_ADC,
               EXT_NTC,
               EXT_CAPACITY,
               EXT_STATUS,
               EXT_SAMPLE_COUNT } ext_sample_t;
#define EXT_ALL_SAMPLES ((1u << EXT_SAMPLE_COUNT) - 1u)

enum { EXT_INHIBIT_DATA = 1u,
       EXT_INHIBIT_EMPTY = 2u,
       EXT_INHIBIT_HOT = 4u,
       EXT_INHIBIT_FAULT = 8u,
       EXT_INHIBIT_PM = 16u };

typedef enum { A2_FREE,
               A2_INSERT_PENDING,
               A2_METER_OWNED,
               A2_REMOVE_PENDING,
               A2_UNCERTAIN } meter_a2_t;

static uint8_t s_ext_requested, s_ext_active, s_ext_valid, s_ext_hot, s_ext_fault;
static uint8_t s_ext_pm_closing, s_ext_pm_work;
static uint16_t s_ext_vbat_mv, s_ext_ibus_ma;
static uint32_t s_ext_stamp[EXT_SAMPLE_COUNT], s_ext_release_at, s_ext_pm_at;
static meter_a2_t s_meter_a2;
static uint8_t s_meter_a1_on, s_meter_real_path, s_meter_real_present;
static uint8_t s_meter_ports_valid;
static uint32_t s_meter_ports_at, s_meter_attempt_at, s_meter_command_at;
static uint32_t s_meter_real_at;

static uint8_t ext_loads(void) {
    return (uint8_t)(s_ext_requested | s_ext_active);
}

static void ext_record_sample(ext_sample_t sample) {
    s_ext_stamp[sample] = Millis();
    s_ext_valid |= (uint8_t)(1u << sample);
}

static uint8_t ext_sample_fresh(ext_sample_t sample, uint32_t now) {
    return (s_ext_valid & (1u << sample)) &&
           (uint32_t)(now - s_ext_stamp[sample]) <= SW_MONITOR_STALE_MS;
}

static uint8_t ext_inhibit(uint32_t now) {
    uint8_t flags = 0u;
    if (!SW6306_IsInitialized() || s_ext_valid != EXT_ALL_SAMPLES) {
        flags |= EXT_INHIBIT_DATA;
    } else {
        for (unsigned i = 0; i < EXT_SAMPLE_COUNT; ++i)
            if ((uint32_t)(now - s_ext_stamp[i]) > SW_MONITOR_STALE_MS)
                flags |= EXT_INHIBIT_DATA;
    }
    if ((ext_sample_fresh(EXT_ADC, now) && s_ext_vbat_mv <= EXT_BATTERY_MIN_MV) ||
        (ext_sample_fresh(EXT_CAPACITY, now) && s_monitor.capacity_pct == 0u))
        flags |= EXT_INHIBIT_EMPTY;
    if (s_ext_hot)
        flags |= EXT_INHIBIT_HOT;
    if (ext_sample_fresh(EXT_STATUS, now) && s_ext_fault)
        flags |= EXT_INHIBIT_FAULT;
    if (s_ext_pm_closing || pm_api_ui_should_block())
        flags |= EXT_INHIBIT_PM;
    return flags;
}

static uint8_t meter_ports_fresh(uint32_t now) {
    return s_meter_ports_valid && (uint32_t)(now - s_meter_ports_at) <= SW_MONITOR_STALE_MS;
}

static uint8_t meter_ready(uint32_t now) {
    return !s_bus_force_off && meter_ports_fresh(now) &&
           (s_meter_real_path || s_meter_a2 == A2_METER_OWNED);
}

uint8_t PowerAlgo_ExtLoadRequest(uint8_t load) {
    /* Missing startup telemetry may settle inside the caller's wait window.
     * A confirmed cutoff is never queued for automatic restart. */
    if (ext_inhibit(Millis()) & ~EXT_INHIBIT_DATA)
        return 0u;
    s_ext_requested |= load;
    idle_reset();
    pm_api_set_sleep_block(EXT_LOAD_PM_BLOCK, 1u);
    return 1u;
}

void PowerAlgo_ExtLoadSetActive(uint8_t load, uint8_t active) {
    if (active) {
        s_ext_requested &= (uint8_t)~load;
        s_ext_active |= load;
        if (s_monitor.charging)
            s_monitor.eta_valid = 0u;
    } else {
        s_ext_active &= (uint8_t)~load;
        s_ext_requested |= load;
    }
}

void PowerAlgo_ExtLoadRelease(uint8_t load) {
    uint8_t was_needed = ext_loads();
    s_ext_requested &= (uint8_t)~load;
    s_ext_active &= (uint8_t)~load;
    if (was_needed && !ext_loads()) {
        s_ext_release_at = Millis();
        pm_api_set_sleep_block(EXT_LOAD_PM_BLOCK, 0u);
    }
}

ext_load_state_t PowerAlgo_ExtLoadState(uint8_t load, uint32_t now) {
    uint8_t inhibit = ext_inhibit(now);
    if ((inhibit & ~EXT_INHIBIT_DATA) ||
        ((s_ext_active & load) && (inhibit || !meter_ports_fresh(now))))
        return EXT_LOAD_BLOCKED;
    return !inhibit && meter_ready(now) ? EXT_LOAD_READY : EXT_LOAD_WAIT;
}

int16_t PowerAlgo_ExtLoadTemperature(void) {
    return s_monitor.ntc_c10;
}

static void meter_note_ports(uint32_t now) {
    uint8_t connections = SW6306_ConnectedPortMask();
    uint8_t a2_on = SW6306_IsPortA2ON() != 0u;
    uint8_t a2_online = (connections & SW6306_CONNECTED_A2) != 0u;
    s_meter_a1_on = SW6306_IsPortA1ON() != 0u;
    uint8_t real_path = SW6306_IsPortC1ON() || SW6306_IsCharging() || s_meter_a1_on;
    if (real_path && (!s_meter_real_path || !meter_ports_fresh(now)))
        s_meter_real_at = now;
    s_meter_real_path = real_path;
    s_meter_real_present = real_path || (connections & (SW6306_CONNECTED_C1 | SW6306_CONNECTED_A1));
    s_meter_ports_at = now;
    s_meter_ports_valid = 1u;

    /* A2 has no physical connector. An unexpected active A2 can be
     * removed, but only a confirmed insertion permits external loads. */
    uint8_t settled = (uint32_t)(now - s_meter_command_at) >= METER_RETRY_MS;
    if (a2_on && s_meter_a2 == A2_INSERT_PENDING) {
        s_meter_a2 = A2_METER_OWNED;
    } else if (!a2_on && !a2_online) {
        if ((s_meter_a2 != A2_INSERT_PENDING && s_meter_a2 != A2_UNCERTAIN) || settled)
            s_meter_a2 = A2_FREE;
    } else if (s_meter_a2 == A2_FREE ||
               (s_meter_a2 == A2_METER_OWNED && !a2_on) ||
               (settled && (s_meter_a2 == A2_INSERT_PENDING || s_meter_a2 == A2_REMOVE_PENDING))) {
        s_meter_a2 = A2_UNCERTAIN;
    }
}

static uint8_t meter_remove_due(uint32_t now) {
    if (s_meter_a2 != A2_METER_OWNED && s_meter_a2 != A2_UNCERTAIN)
        return 0u;
    if (!ext_loads())
        return (uint32_t)(now - s_ext_release_at) >= METER_RELEASE_MS;
    if (s_meter_real_path)
        return (uint32_t)(now - s_meter_real_at) >= METER_HANDOVER_MS;
    return s_meter_a2 == A2_UNCERTAIN && !ext_inhibit(now);
}

static uint8_t meter_due(uint32_t now) {
    if ((uint32_t)(now - s_meter_attempt_at) < METER_RETRY_MS)
        return 0u;
    if (meter_remove_due(now))
        return 1u;
    if (ext_loads())
        return !ext_inhibit(now) && !meter_ready(now);
    return (uint32_t)(now - s_ext_release_at) >= METER_RELEASE_MS && s_meter_a2 != A2_FREE;
}

static void ext_cache_adc(void) {
    s_ext_valid &= (uint8_t)~(1u << EXT_ADC);
    if (SW6306_LastOperationOK() && SW6306_IsInitialized()) {
        s_ext_vbat_mv = SW6306_ReadVBAT();
        s_ext_ibus_ma = SW6306_ReadIBUS();
        if (s_ext_vbat_mv)
            ext_record_sample(EXT_ADC);
    }
}

static void ext_cache_status(void) {
    s_ext_valid &= (uint8_t)~(1u << EXT_STATUS);
    if (!s_status_fresh || !SW6306_IsInitialized())
        return;
    sw6306_fault_snapshot_t fault;
    SW6306_GetFaultSnapshot(&fault);
    /* Live stop flags qualify the battery/thermal reason groups.
     * A BUS-only overload or short is local to the SW6306 port. */
    s_ext_fault =
        (SW6306_IsDischargeStoppedByFault() &&
         (fault.discharge & (SW6306_FAULT1_OV_BAT | SW6306_FAULT1_OT_CHIP | SW6306_FAULT1_OT_NTC))) ||
        (SW6306_IsChargeStoppedByFault() &&
         (fault.charge & (SW6306_FAULT2_BATLOW | SW6306_FAULT2_OV_BAT | SW6306_FAULT2_OT_CHIP | SW6306_FAULT2_OT_NTC)));
    ext_record_sample(EXT_STATUS);
}

static void sw6306_bus_read_failed(void) {
    s_cycle_ok = s_bus_display_valid = s_monitor.eta_valid = 0u;
    idle_reset();
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
    uint8_t connected = SW6306_ConnectedPortMask() & (uint8_t)~SW6306_CONNECTED_A2;
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
    s_meter_ports_valid = 0u;
    idle_reset();
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
               AUX_STATE_WAIT_METER,
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
static uint32_t s_aux_edge_at, s_aux_sample_at, s_aux_low_since, s_aux_wait_at;
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
    PowerAlgo_ExtLoadRelease(EXT_LOAD_AUX);
}

static void adc_batch_begin(uint32_t now) {
    s_adc_batch_at = s_display_sample_at = now;
    s_adc_sample_count = 0u;
    s_scan_for_display = 0u;
    memset(s_adc_accumulator, 0, sizeof(s_adc_accumulator));
}

static void aux_enable(void) {
    if (!s_aux_allowed || s_aux_state != AUX_STATE_OFF || !s_adc_initialized ||
        !PowerAlgo_ExtLoadRequest(EXT_LOAD_AUX))
        return;
    s_aux_state = AUX_STATE_WAIT_METER;
    s_aux_wait_at = Millis();
    aux_publish(0u, 0u, 0u);
}

static void aux_start(void) {
    PowerAlgo_ExtLoadSetActive(EXT_LOAD_AUX, 1u);
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port, SUB_EN_Pin);
    uint32_t now = Millis();
    s_aux_sample_at = now + AUX_STARTUP_MS;
    s_aux_state = AUX_STATE_STARTUP;
    s_monitor.aux_enabled = 1u;
    aux_publish(0u, 0u, 0u);
    /* Discard samples taken with the previous output state. */
    adc_batch_begin(now);
}

void PowerAlgo_AuxA_Request(aux_command_t command) {
    if (!s_aux_allowed)
        return;
    s_aux_command = command;
    s_aux_command_pending = 1u;
}

void PowerAlgo_AuxA_SetAutoOffCurrent(unsigned threshold_ma) {
    s_aux_auto_off_ma = threshold_ma;
    s_aux_low_current = 0u;
}

void PowerAlgo_AuxA_OnEvents(uint8_t events) {
    if (s_aux_allowed && (events & PM_WAKE_SUB_INDET) && !s_aux_latched) {
        s_aux_edge_at = Millis();
        s_aux_candidate = 1u;
    }
}

uint8_t PowerAlgo_AuxA_TakeInserted(void) {
    uint8_t inserted = s_aux_allowed && s_aux_inserted;
    s_aux_inserted = 0u;
    return inserted;
}

static void aux_service(uint32_t now) {
    if (!s_aux_allowed) {
        s_aux_candidate = s_aux_command_pending = s_aux_inserted = 0u;
        s_aux_latched = !LL_GPIO_IsInputPinSet(SUB_INDET_GPIO_Port, SUB_INDET_Pin);
        if (s_aux_state != AUX_STATE_OFF)
            aux_disable();
        return;
    }
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
        if (s_aux_command == AUX_CMD_DISABLE ||
            (s_aux_command == AUX_CMD_TOGGLE && s_aux_state != AUX_STATE_OFF))
            aux_disable();
        else
            aux_enable();
    } else if (inserted) {
        aux_enable();
    }

    if (s_aux_state == AUX_STATE_OFF)
        return;
    ext_load_state_t state = PowerAlgo_ExtLoadState(EXT_LOAD_AUX, now);
    if (state == EXT_LOAD_BLOCKED) {
        aux_disable();
    } else if (s_aux_state == AUX_STATE_WAIT_METER) {
        if (state == EXT_LOAD_READY)
            aux_start();
        else if ((uint32_t)(Millis() - s_aux_wait_at) >= EXT_LOAD_WAIT_MS)
            aux_disable();
    } else if (state == EXT_LOAD_WAIT) {
        aux_disable();
        aux_enable();
    }
}

static pm_hook_result_t ext_load_prepare(void *ctx) {
    (void)ctx;
    if (!s_ext_pm_closing) {
        idle_reset();
        s_ext_pm_closing = 1u;
        s_ext_pm_at = Millis();
        WLED_AlgoStop();
        aux_disable();
        s_aux_command_pending = 0u;
        /* An existing in-flight A2 operation must settle before sleep. */
        s_meter_attempt_at = s_ext_pm_at - METER_RETRY_MS;
    }
    if (!s_sw_owner_busy && s_meter_a2 == A2_FREE) {
        s_ext_pm_work = 0u;
        return PM_HOOK_DONE;
    }
    if (!SW6306_IsInitialized() || (uint32_t)(Millis() - s_ext_pm_at) >= EXT_LOAD_WAIT_MS) {
        s_ext_pm_work = 0u;
        return PM_HOOK_ERROR;
    }
    s_ext_pm_work = 1u;
    return PM_HOOK_PENDING;
}

static pm_hook_result_t ext_load_resume(void *ctx) {
    (void)ctx;
    s_ext_pm_work = 0u;
    if (s_ext_pm_closing) {
        s_ext_pm_closing = 0u;
        s_ext_valid = 0u;
        s_meter_ports_valid = 0u;
    }
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_ext_load_pm = {
    "external-load", ext_load_prepare, 0, ext_load_resume, 0, 80u, 0, 0};

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
    s_scan_for_protection = s_monitor.aux_enabled &&
                            (int32_t)(now - s_aux_sample_at) >= 0;
    /* When AUX is active, use the same scan for both paths. The first
     * protection scan starts only after the startup deadline. */
    if (!(s_monitor.aux_enabled ? s_scan_for_protection : s_scan_for_display))
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
    ext_cache_adc();
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
    ext_cache_status();
    s_ext_valid &= (uint8_t)~(1u << EXT_NTC);
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
    if (c10 >= EXT_NTC_STOP_C10)
        s_ext_hot = 1u;
    else if (c10 <= EXT_NTC_RESTART_C10)
        s_ext_hot = 0u;
    ext_record_sample(EXT_NTC);
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
    s_ext_valid &= (uint8_t)~(1u << EXT_CAPACITY);
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
    ext_record_sample(EXT_CAPACITY);
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
    if (!s_monitor.valid || !mode || power_mw < 200u || (s_monitor.charging && s_ext_active)) {
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

/* All meter I2C operations run inside PowerAlgo_SW6306Thread.
 * The PM hook merely requests cleanup and waits for its result. */
THRD_DECLARE(meter_read_ports) {
    static uint32_t before;

    THRD_BEGIN;
    before = SW6306_PortReadSequence();
    SW6306_OperationReset();
    THRD_SPAWN_NOARG(SW6306_PortStatusLoad);
    if (SW6306_LastOperationOK() && SW6306_PortReadSequence() != before) {
        meter_note_ports(Millis());
        sw6306_connection_update();
    } else {
        s_meter_ports_valid = 0u;
    }
    THRD_END;
}

THRD_DECLARE(meter_step) {
    THRD_BEGIN;
    s_meter_attempt_at = Millis();
    THRD_SPAWN_NOARG(meter_read_ports);
    if (!s_meter_ports_valid)
        PT_EXIT(pt);

    if (meter_remove_due(Millis())) {
        s_meter_a2 = A2_UNCERTAIN;
        SW6306_OperationReset();
        THRD_SPAWN_NOARG(SW6306_PortA2Remove);
        s_meter_command_at = Millis();
        if (SW6306_LastOperationOK())
            s_meter_a2 = A2_REMOVE_PENDING;
        THRD_SPAWN_NOARG(meter_read_ports);
    } else if (ext_loads() && !ext_inhibit(Millis()) &&
               !s_meter_real_present && s_meter_a2 == A2_FREE) {
        /* A2 auto-off is already disabled by SW6306_Init. A1 keeps its
         * normal hardware idle policy throughout external metering. */
        s_meter_a2 = A2_UNCERTAIN;
        SW6306_OperationReset();
        THRD_SPAWN_NOARG(SW6306_PortA2Insert);
        s_meter_command_at = Millis();
        if (SW6306_LastOperationOK())
            s_meter_a2 = A2_INSERT_PENDING;
        THRD_SPAWN_NOARG(meter_read_ports);
    }
    THRD_END;
}

/* Only the SW6306 owner consumes dirty bits. One register operation or
 * power transaction per service slot leaves the monitor and ADC responsive. */
static uint8_t config_ports_idle(uint32_t now) {
    return s_idle_verified && meter_ports_fresh(now) && SW6306_TransportPortsIdle() &&
           !ext_loads() && s_meter_a2 == A2_FREE;
}

static uint32_t config_eligible(uint32_t now) {
    uint32_t mask = CFG_POWER_MASK | CFG_BROADCAST_MASK;
    if (config_ports_idle(now))
        mask |= CFG_CHIP_SWITCH_MASK;
    return s_config_dirty & mask;
}

static void config_restore_switches(void) {
    /* Init writes the ON baseline. Replay any user-disabled switches once
     * the ports are idle again. Pending UI requests survive reinit. */
    for (unsigned i = CFG_C1_INPUT; i < CFG_COUNT; ++i) {
        uint32_t bit = CFG_BIT(i);
        if ((CFG_CHIP_SWITCH_MASK & bit) && !s_setting_switch[i])
            s_config_dirty |= bit;
    }
}

THRD_DECLARE(config_step) {
    static uint32_t selected;
    static unsigned index;
    static const setting_desc_t *desc;
    static uint8_t value, value2, value3;
    THRD_BEGIN;

    selected = config_eligible(Millis());
    if (!selected)
        PT_EXIT(pt);
    /* Power and explicit broadcasts are independent of the deferred switches. */
    if (selected & (CFG_POWER_MASK | CFG_BROADCAST_MASK))
        selected &= CFG_POWER_MASK | CFG_BROADCAST_MASK;
    for (index = 0u; !(selected & CFG_BIT(index)); ++index) {
    }
    selected = CFG_BIT(index);
    desc = &s_settings[index];
    value = s_setting_switch[index];
    if (desc->kind == SET_PD_YIELD) {
        selected = CFG_YIELD_MASK;
        value = s_setting_switch[CFG_PD_SCP];
        value2 = s_setting_switch[CFG_PD_VOOC];
        value3 = s_setting_switch[CFG_PD_UFCS];
    } else if (desc->kind == SET_SCP) {
        selected = CFG_SCP_MASK;
        value = s_setting_switch[CFG_HVSCP];
        value2 = s_setting_switch[CFG_LVSCP];
    } else if (index == CFG_OUTPUT) {
        value = s_output_requested;
    } else if (index == CFG_INPUT) {
        value = s_input_requested;
    }

    /* Snapshot before yielding; a new UI change sets its bit again. */
    s_config_dirty &= ~selected;
    SW6306_OperationReset();
    /* Protothreads resume through an enclosing switch: keep all
     * wait points outside any nested switch. */
    if (desc->kind == SET_PORT) {
        THRD_SPAWN_ARGS(SW6306_PortFastChargeSet, (sw6306_port_t)desc->arg0,
                        (sw6306_port_dir_t)desc->arg1, value);
    } else if (desc->kind == SET_PROTOCOL) {
        THRD_SPAWN_ARGS(SW6306_ProtocolEnable, (sw6306_proto_t)desc->arg0,
                        (sw6306_proto_dir_t)desc->arg1, value);
    } else if (desc->kind == SET_PD_YIELD) {
        THRD_SPAWN_ARGS(SW6306_PDSourceYieldSet, value, value2, value3);
    } else if (desc->kind == SET_PPS) {
        THRD_SPAWN_ARGS(SW6306_PPSEnable, (sw6306_pps_t)desc->arg0, value);
    } else if (desc->kind == SET_SCP) {
        THRD_SPAWN_ARGS(SW6306_SCPSourceSet, value, value2);
    } else if (index == CFG_OUTPUT) {
        THRD_SPAWN_ARGS(SW6306_SetMaxOutputPower, value);
    } else if (index == CFG_INPUT) {
        THRD_SPAWN_ARGS(SW6306_SetMaxInputPower, value);
    } else if (index == CFG_PPS_BROADCAST) {
        THRD_SPAWN_NOARG(SW6306_PPSBroadcast);
    } else if (index == CFG_UFCS_BROADCAST) {
        THRD_SPAWN_NOARG(SW6306_UFCSBroadcast);
    }
    if (!SW6306_LastOperationOK())
        s_config_dirty |= selected;
    THRD_END;
}

static uint8_t bus_idle_due(uint32_t now) {
    if ((unsigned)s_idle_timeout >= sizeof(s_idle_seconds) / sizeof(s_idle_seconds[0]) ||
        !SW6306_IsInitialized() || !s_cycle_ok || !s_adc_fresh || !s_status_fresh ||
        !ext_sample_fresh(EXT_ADC, now) || !ext_sample_fresh(EXT_STATUS, now) ||
        !meter_ports_fresh(now) || SW6306_IsCharging() || SW6306_IsLowCurrentMode() ||
        ext_loads() || s_meter_a2 != A2_FREE ||
        (!SW6306_IsPortC1ON() && !SW6306_IsPortA1ON()) ||
        s_ext_ibus_ma > (unsigned)s_idle_threshold) {
        idle_reset();
        return 0u;
    }
    if (s_idle_fired)
        return 0u;
    if (!s_idle_running) {
        s_idle_running = 1u;
        s_idle_since = now;
        return 0u;
    }
    return (uint32_t)(now - s_idle_since) >= s_idle_seconds[s_idle_timeout] * 1000u;
}

/************************ SW6306 定期采样与缓存 ************************/
/* One actor owns the SW6306 PT call tree. Every I2C operation now
 * submits through HAL IRQ and yields only this coroutine while waiting.
 * Allow already-started transfers to finish during UI_OFF preparation;
 * never start new background transactions until RUN is restored. */
char PowerAlgo_SW6306Thread(coro_pt_t *pt) {
    THRD_BEGIN;
    for(;;) {
        if (s_sw_low_power ||
            (pm_api_ui_should_block() && !s_bg_radio_reads_allowed && !s_ext_pm_work)) {
            THRD_DELAY(20u);
            continue;
        }
        if (s_ext_pm_work) {
            if (SW6306_IsInitialized() && meter_due(Millis())) {
                s_sw_owner_busy = 1u;
                THRD_SPAWN_NOARG(meter_step);
                s_sw_owner_busy = 0u;
            }
            THRD_DELAY(50u);
            continue;
        }
        if(!SW6306_IsInitialized()) {
            s_ext_valid = 0u;
            s_meter_ports_valid = 0u;
            if (s_meter_a2 != A2_FREE)
                s_meter_a2 = A2_UNCERTAIN;
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
            s_sw_low_power = 0u;
            s_sw_phase=0u;
            s_capacity_div=1u;
            s_status_fresh=0u;
            s_cycle_ok=1u;
            s_monitor.initialized=1u;
            config_restore_switches();
            idle_reset();
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
            if (SW6306_LastOperationOK() && SW6306_PortReadSequence() != s_sw_seq_before) {
                meter_note_ports(Millis());
                sw6306_connection_update();
            } else {
                s_meter_ports_valid = 0u;
            }
            s_idle_verified=(uint8_t)(SW6306_LastOperationOK() &&
                SW6306_PortReadSequence()!=s_sw_seq_before &&
                SW6306_TransportPortsIdle());
            s_monitor.port_busy=(uint8_t)!s_idle_verified;
            if (!SW6306_LastOperationOK())
                sw6306_bus_read_failed();
            pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,!s_idle_verified);
            if(!s_idle_verified)pm_api_refresh_idle();
        } else if (s_sw_phase == 3u) {
            if (meter_due(Millis())) {
                idle_reset();
                s_sw_owner_busy = 1u;
                THRD_SPAWN_NOARG(meter_step);
                s_sw_owner_busy = 0u;
            } else if (bus_idle_due(Millis())) {
                /* Keep new external load requests waiting until the
                 * asynchronous output removal has settled and been reread. */
                s_bus_force_off = 1u;
                s_sw_owner_busy = 1u;
                SW6306_OperationReset();
                THRD_SPAWN_NOARG(SW6306_ForceOff);
                s_sw_owner_busy = 0u;
                s_idle_fired = SW6306_LastOperationOK();
                s_meter_ports_valid = 0u;
                s_idle_verified = 0u;
                s_bus_force_off = 0u;
            } else if (config_eligible(Millis())) {
                s_sw_owner_busy = 1u;
                THRD_SPAWN_NOARG(config_step);
                s_sw_owner_busy = 0u;
            }
        } else if (s_sw_phase == 4u) {
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_PowerLoad);
            s_sw_owner_busy=0u;
            if (!SW6306_LastOperationOK())
                sw6306_bus_read_failed();
            else if (!SW6306_ValidateInitialized()) {
                sw6306_display_read_failed();
                s_ext_valid = 0u;
                s_meter_ports_valid = 0u;
                s_idle_verified = 0u;
                idle_reset();
                pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK, 1u);
                s_retry_at = Millis();
                continue;
            }
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
                s_monitor.discharging = SW6306_IsDischarging() ||
                                        (!s_monitor.charging && s_ext_active && meter_ready(Millis()));
                s_monitor.protocol = SW6306_ReadProtocol();
                s_monitor.sample_ms = Millis();
                sw6306_update_eta(s_monitor.sample_ms);
            }
            s_status_fresh=0u;
        }
        /* Phase 3 uses the idle I2C slot only when the meter has work. */
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
    out->a1_on = s_meter_a1_on;
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
    s_meter_attempt_at = Millis() - METER_RETRY_MS;
    (void)pm_device_register(&s_ext_load_pm); /* Stop external loads before SW6306. */
    (void)pm_device_register(&s_node);         /* SW6306 / order 90 */
    (void)pm_device_register(&s_power_adc_pm); /* SUB_EN + ADC / order 200 */
}

void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c10,uint8_t *valid) {
    if(vdda_mv)*vdda_mv=s_vdda_mv;
    if(die_temp_c10)*die_temp_c10=s_die_temp_c10;
    if(valid)*valid=(uint8_t)(s_system_adc_valid && !s_suspended);
}
