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
#include "battery_algo.h"
#include <string.h>
#include "mini_format.h"

#define POWER_SW_SLEEP_BLOCK 0x08u /* separate from scanner (0x02) and radio (0x04) */
#define SW_REINIT_DELAY_MS 5000u
#define SW_MONITOR_STALE_MS 2500u

enum {
    BATTERY_CHARGE,
    BATTERY_VOLTAGE,
    BATTERY_MAX_ENERGY,
    BATTERY_REMAIN_ENERGY,
    BATTERY_TEMP,
    BATTERY_SOH,
    BATTERY_EFC,
    BATTERY_LEARN,
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
static uint32_t s_retry_at, s_sw_next_at;
static volatile uint8_t s_meter_irq; /* PB5 shared INT: ISR sets, owner consumes. */
static uint8_t s_scene_seen;
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
    [CFG_OUTPUT] = {" Output Power Limit W", SET_NUMBER},
    [CFG_INPUT] = {" Input Power Limit W", SET_NUMBER},
    [CFG_C1_INPUT] = {" C1 Input Fast Charge", SET_PORT, SW6306_PORT_C1, SW6306_PORT_DIR_IN},
    [CFG_C1_OUTPUT] = {" C1 Output Fast Charge", SET_PORT, SW6306_PORT_C1, SW6306_PORT_DIR_OUT},
    [CFG_A1_OUTPUT] = {" A1 Output Fast Charge", SET_PORT, SW6306_PORT_A1, SW6306_PORT_DIR_OUT},
    [CFG_IDLE_TIMEOUT] = {" Idle Shutdown Timeout", SET_ENUM},
    [CFG_IDLE_THRESHOLD] = {" Idle Shutdown Current mA", SET_NUMBER},
    [CFG_AUX_ENABLE] = {" Aux-A Port Enable", SET_AUX_SWITCH},
    [CFG_AUX_THRESHOLD] = {" Aux-A Idle Current mA", SET_NUMBER},
    [CFG_PD_OUT] = PROTO(" PD Output", PD, SOURCE),
    [CFG_PD_IN] = PROTO(" PD Input", PD, SINK),
    [CFG_PD_SCP] = {" PD Yield to SCP", SET_PD_YIELD},
    [CFG_PD_VOOC] = {" PD Yield to VOOC", SET_PD_YIELD},
    [CFG_PD_UFCS] = {" PD Yield to UFCS", SET_PD_YIELD},
    [CFG_PPS_BROADCAST] = {" PPS Broadcast", SET_BROADCAST},
    [CFG_PPS_11] = {" 11V PPS", SET_PPS, SW6306_PPS_1},
    [CFG_PPS_21] = {" 21V PPS", SET_PPS, SW6306_PPS_3},
    [CFG_UFCS_BROADCAST] = {" UFCS Broadcast", SET_BROADCAST},
    [CFG_UFCS_OUT] = PROTO(" UFCS Output", UFCS, SOURCE),
    [CFG_UFCS_IN] = PROTO(" UFCS Input", UFCS, SINK),
    [CFG_QC] = PROTO(" QC", QC, SOURCE),
    [CFG_FCP] = PROTO(" FCP", FCP, SOURCE),
    [CFG_AFC_OUT] = PROTO(" AFC Output", AFC, SOURCE),
    [CFG_AFC_IN] = PROTO(" AFC Input", AFC, SINK),
    [CFG_HVSCP] = {" HVSCP Output", SET_SCP},
    [CFG_LVSCP] = {" LVSCP Output", SET_SCP},
    [CFG_SCP_IN] = PROTO(" SCP Input", SCP, SINK),
    [CFG_PE] = PROTO(" PE", PE, SOURCE),
    [CFG_SFCP] = PROTO(" SFCP", SFCP, SOURCE),
    [CFG_VOOC_OUT] = PROTO(" VOOC Output", VOOC, SOURCE),
    [CFG_VOOC_IN] = PROTO(" VOOC Input", VOOC, SINK),
    [CFG_SVOOC] = PROTO(" SVOOC", SVOOC, SOURCE),
};
#undef PROTO

static const unsigned s_idle_seconds[] = {5u, 10u, 30u, 60u, 300u, 900u, 1800u};
static const char *const s_idle_labels[] = {"5s", "10s", "30s", "1min", "5min", "15min", "30min", "Off"};
static int s_output_w = SW6306_OUTPUT_POWER_MAX, s_input_w = SW6306_INPUT_POWER_MAX;
static uint8_t s_output_requested = SW6306_OUTPUT_POWER_MAX, s_input_requested = SW6306_INPUT_POWER_MAX;
static int s_idle_timeout = 3, s_idle_threshold = 12, s_aux_threshold = 50; /* 1 min */
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
                data->name = i == CFG_OUTPUT ? "Output W" : "Input W";
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

typedef enum { METER_A2_UNKNOWN,
               METER_A2_OFF,
               METER_A2_ON,
               METER_A2_PENDING } meter_a2_state_t;

static uint8_t s_ext_requested, s_ext_active, s_ext_valid, s_ext_hot, s_ext_fault;
static uint8_t s_ext_pm_closing, s_ext_pm_work;
static uint16_t s_ext_vbat_mv, s_ext_ibus_ma;
static uint32_t s_ext_stamp[EXT_SAMPLE_COUNT], s_ext_release_at, s_ext_pm_at;
static meter_a2_state_t s_meter_a2;
static uint8_t s_a2_target; /* Latched submitted command, not current demand. */
static uint8_t s_meter_a1_on, s_meter_real_path, s_meter_real_present;
static uint8_t s_meter_ports_valid;
static uint32_t s_meter_ports_at, s_meter_attempt_at, s_meter_command_at;


/* Fault history and notification are independent of protection decisions. */
enum {
    FAULT_SW,
    FAULT_AUX,
    FAULT_METER,
    FAULT_BATTERY,
    FAULT_NTC,
    FAULT_OFFLINE,
    FAULT_WLED
};
enum {
    NOTICE_LEARN_DONE = 100u,
    NOTICE_LOW_CURRENT = 101u,
    NOTICE_DATA_INVALID = 102u
};
#define FAULT_EVENTS (SW6306_FAULT0_UVLO | SW6306_FAULT0_CHGERR | SW6306_FAULT0_DISCHGERR)
#define FAULT_LIVE (SW6306_SYS_STAT_CHGERR | SW6306_SYS_STAT_DISCHGERR)
static power_fault_view_t s_fault;
static power_notice_t s_notice;
static uint8_t s_notice_pending, s_fault_source, s_fault_live_mask;
static uint8_t s_fault_seen_events, s_fault_prev_live, s_fault_recovered, s_fault_status_ok, s_fault_has_snapshot;
static uint8_t s_fault_offline, s_fault_init_failed, s_fault_meter_timeout;
static uint16_t s_fault_common_seen, s_fault_common_software;
static uint8_t s_fault_clear_mask;
static uint32_t s_fault_status_at;
static sw6306_fault_snapshot_t s_fault_latest;
static ui_item_t s_fault_items[3];
static char s_fault_labels[3][22];

const char *PowerAlgo_NoticeText(uint8_t code) {
    switch (code) {
    case 1:
        return "BAT UVLO";
    case 2:
        return "BAT OVERVOLT";
    case 3:
        return "CHIP OVERHEAT";
    case 4:
        return "NTC OVERHEAT";
    case 5:
        return "VBUS SHORT";
    case 6:
        return "VBUS OVERLOAD";
    case 7:
        return "VBUS OVERVOLT";
    case 8:
        return "CHARGE TIMEOUT";
    case 9:
        return "BAT TOO LOW";
    case 10:
        return "DPDM OVERVOLT";
    case 11:
        return "CC OVERVOLT";
    case 12:
        return "TEMP LIMIT";
    case 13:
        return "POWER PROTECT";
    case 20:
        return "AUX SHORT";
    case 21:
        return "AUX ADC ERROR";
    case 30:
        return "WLED TEMP LIMIT";
    case 40:
        return "METER TIMEOUT";
    case 41:
        return "BAT PROTECT";
    case 50:
        return "SW6306 OFFLINE";
    case NOTICE_LEARN_DONE:
        return "CAP LEARN DONE";
    case NOTICE_LOW_CURRENT:
        return "LOW CURRENT ERROR";
    case NOTICE_DATA_INVALID:
        return "DATA INVALID";
    default:
        return "NO FAULT";
    }
}

static uint8_t fault_severity(uint8_t code) {
    return code == 6u || code == 8u || code == 12u || code == 40u ? POWER_NOTICE_WARNING : POWER_NOTICE_FAULT;
}

static void fault_offer_notice(uint8_t code, uint8_t severity) {
    if (!s_notice_pending || severity >= s_notice.severity) {
        s_notice.code = code;
        s_notice.severity = severity;
        s_notice_pending = 1u;
    }
}

uint8_t PowerAlgo_NoticePending(void) {
    return s_notice_pending;
}

uint8_t PowerAlgo_TakeNotice(power_notice_t *out) {
    if (!out || !s_notice_pending)
        return 0u;
    *out = s_notice;
    s_notice_pending = 0u;
    return 1u;
}

static void fault_report(uint8_t code, uint8_t source) {
    s_fault.code = code;
    s_fault.severity = fault_severity(code);
    s_fault.raw = s_fault_latest;
    s_fault_source = source;
    s_fault_live_mask = 0u;
    if (s_fault.count != UINT16_MAX)
        ++s_fault.count;
    fault_offer_notice(code, s_fault.severity);
}

/* Reasons are historical; qualify them by the observed event / live
 * direction. Prefer newly changed reason bits over older latched bits. */
static uint8_t fault_decode(sw6306_fault_snapshot_t snap, uint8_t directions) {
    if (!(directions & SW6306_SYS_STAT_DISCHGERR))
        snap.discharge = 0u;
    if (!(directions & SW6306_SYS_STAT_DISCHGERR))
        snap.extra &= SW6306_FAULT3_CHG_62368UT | SW6306_FAULT3_CHG_62368OT;
    if (!(directions & SW6306_SYS_STAT_CHGERR))
        snap.charge = 0u;
    if (!(directions & SW6306_SYS_STAT_CHGERR))
        snap.extra &= (uint8_t)~(SW6306_FAULT3_CHG_62368UT | SW6306_FAULT3_CHG_62368OT);
    if ((snap.discharge & SW6306_FAULT1_OV_BAT) || (snap.charge & SW6306_FAULT2_OV_BAT))
        return 2u;
    if ((snap.discharge & SW6306_FAULT1_OT_CHIP) || (snap.charge & SW6306_FAULT2_OT_CHIP))
        return 3u;
    if ((snap.discharge & SW6306_FAULT1_OT_NTC) || (snap.charge & SW6306_FAULT2_OT_NTC))
        return 4u;
    if (snap.discharge & SW6306_FAULT1_SHT_BUS)
        return 5u;
    if (snap.discharge & SW6306_FAULT1_OL_BUS)
        return 6u;
    if ((snap.discharge & (SW6306_FAULT1_SOV_BUS | SW6306_FAULT1_FOV_BUS)) ||
        (snap.charge & (SW6306_FAULT2_SOV_BUS | SW6306_FAULT2_FOV_BUS)))
        return 7u;
    if (snap.charge & SW6306_FAULT2_OT)
        return 8u;
    if (snap.charge & SW6306_FAULT2_BATLOW)
        return 9u;
    if (snap.extra & SW6306_FAULT3_OV_DPDM)
        return 10u;
    if (snap.extra & SW6306_FAULT3_OV_CC)
        return 11u;
    if ((snap.extra & (SW6306_FAULT3_DCHG_62368UT | SW6306_FAULT3_CHG_62368UT | SW6306_FAULT3_CHG_62368OT)))
        return 12u;
    return 13u;
}

static uint8_t fault_event_directions(uint8_t events) {
    uint8_t directions = 0u;
    if (events & (SW6306_FAULT0_UVLO | SW6306_FAULT0_DISCHGERR))
        directions |= SW6306_SYS_STAT_DISCHGERR;
    if (events & SW6306_FAULT0_CHGERR)
        directions |= SW6306_SYS_STAT_CHGERR;
    return directions;
}

static uint16_t fault_common_bit(uint8_t code) {
    if (code == 41u || code == 1u)
        return 1u << 1;
    if (code == 2u || code == 3u || code == 4u || code == 9u)
        return (uint16_t)(1u << code);
    return 0u;
}

static void fault_hardware_report(uint8_t code, uint8_t directions) {
    /* If the same software protection was already reported, attach its
     * hardware evidence rather than counting the same episode twice. */
    uint16_t common = fault_common_bit(code);
    if (!(s_fault_common_software & common)) {
        fault_report(code, FAULT_SW);
    } else if (fault_common_bit(s_fault.code) == common &&
               (s_fault_source == FAULT_NTC || s_fault_source == FAULT_BATTERY)) {
        s_fault.code = code;
        s_fault.severity = fault_severity(code);
        s_fault.raw = s_fault_latest;
        s_fault_source = FAULT_SW;
    }
    if (s_fault.code == code && s_fault_source == FAULT_SW)
        s_fault_live_mask = directions;
    s_fault_common_seen |= common;
    s_fault_common_software &= (uint16_t)~common;
}

static uint8_t fault_direction_code(sw6306_fault_snapshot_t changed, sw6306_fault_snapshot_t snap, uint8_t direction) {
    uint8_t code = fault_decode(changed, direction);
    return code == 13u ? fault_decode(snap, direction) : code;
}

static void fault_consume_status(void) {
    sw6306_fault_snapshot_t snap;
    SW6306_GetFaultSnapshot(&snap);
    /* LEARNEND belongs to the capacity-learning consumer, not fault handling. */
    uint8_t events = snap.event0 & FAULT_EVENTS;
    uint8_t fresh = events & (uint8_t)~s_fault_seen_events;
    uint8_t live = snap.system & FAULT_LIVE;
    uint8_t rising = live & (uint8_t)~s_fault_prev_live;
    uint8_t falling = s_fault_prev_live & (uint8_t)~live;
    s_fault_recovered |= falling;
    /* A live edge belonging to an already consumed uncleared event is
     * not another occurrence. Successful W1C re-arms that event bit. */
    uint8_t directions = fault_event_directions(fresh) |
                         (rising & ((uint8_t)~fault_event_directions(events & s_fault_seen_events) |
                                    s_fault_recovered));
    s_fault_recovered &= (uint8_t)~rising;
    sw6306_fault_snapshot_t changed = snap;
    if (s_fault_has_snapshot) {
        changed.discharge &= (uint8_t)~s_fault_latest.discharge;
        changed.charge &= (uint8_t)~s_fault_latest.charge;
        changed.extra &= (uint8_t)~s_fault_latest.extra;
    }
    s_fault_latest = snap;
    s_scene_seen |= snap.event0 & SW6306_FAULT0_SCENE;
    s_fault_status_at = Millis();
    s_fault_status_ok = s_fault_has_snapshot = 1u;
    s_fault_offline = 0u;
    /* A recorded hardware protection that has recovered must not
     * become ACTIVE again due to a different protection later. */
    s_fault_live_mask &= (uint8_t)~falling;
    uint8_t charge_code = (directions & SW6306_SYS_STAT_CHGERR) ? fault_direction_code(changed, snap, SW6306_SYS_STAT_CHGERR) : 0u;
    uint8_t discharge_code = (directions & SW6306_SYS_STAT_DISCHGERR) ? fault_direction_code(changed, snap, SW6306_SYS_STAT_DISCHGERR) : 0u;
    if (fresh & SW6306_FAULT0_UVLO)
        discharge_code = 1u;
    if (charge_code && charge_code == discharge_code) {
        fault_hardware_report(charge_code, directions);
    } else {
        /* Count distinguishable charge/discharge events separately.
         * On a simultaneous snapshot the higher severity is the latest
         * record; the mailbox independently preserves priority. */
        if (charge_code && discharge_code &&
            fault_severity(charge_code) > fault_severity(discharge_code)) {
            fault_hardware_report(discharge_code, SW6306_SYS_STAT_DISCHGERR);
            fault_hardware_report(charge_code, SW6306_SYS_STAT_CHGERR);
        } else {
            if (charge_code)
                fault_hardware_report(charge_code, SW6306_SYS_STAT_CHGERR);
            if (discharge_code)
                fault_hardware_report(discharge_code, SW6306_SYS_STAT_DISCHGERR);
        }
    }
    s_fault_seen_events = events;
    s_fault_prev_live = live;
    s_fault_clear_mask = events;
}

static void fault_offline(void) {
    if (!s_fault_offline) {
        s_fault_offline = 1u;
        fault_report(50u, FAULT_OFFLINE);
    }
    s_fault_status_ok = 0u;
}

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

static uint8_t fault_status_fresh(uint32_t now) {
    return s_fault_status_ok && SW6306_IsInitialized() &&
           (uint32_t)(now - s_fault_status_at) <= SW_MONITOR_STALE_MS;
}

static uint8_t ext_fault_code(uint8_t inhibit) {
    if (inhibit & EXT_INHIBIT_HOT)
        return 4u;
    if (inhibit & EXT_INHIBIT_FAULT) {
        sw6306_fault_snapshot_t snap = s_fault_latest;
        snap.discharge &= SW6306_FAULT1_OV_BAT | SW6306_FAULT1_OT_CHIP | SW6306_FAULT1_OT_NTC;
        snap.charge &= SW6306_FAULT2_OV_BAT | SW6306_FAULT2_OT_CHIP | SW6306_FAULT2_OT_NTC | SW6306_FAULT2_BATLOW;
        snap.extra = 0u;
        uint8_t code = fault_decode(snap, s_fault_prev_live);
        return code == 2u || code == 3u || code == 4u || code == 9u ? code : 41u;
    }
    if (inhibit & EXT_INHIBIT_EMPTY)
        return ext_sample_fresh(EXT_ADC, Millis()) && s_ext_vbat_mv <= EXT_BATTERY_MIN_MV ? 1u : 41u;
    return 0u;
}

void PowerAlgo_ExtLoadReportBlocked(void) {
    uint8_t inhibit = ext_inhibit(Millis());
    if (inhibit & EXT_INHIBIT_PM)
        return;
    uint8_t code = ext_fault_code(inhibit);
    if (!code) {
        fault_offer_notice(s_fault_offline ? 50u : NOTICE_DATA_INVALID,
                           s_fault_offline ? POWER_NOTICE_FAULT : POWER_NOTICE_WARNING);
        return;
    }
    uint16_t bit = fault_common_bit(code);
    if (!(s_fault_common_seen & bit)) {
        fault_report(code, inhibit & EXT_INHIBIT_HOT ? FAULT_NTC : FAULT_BATTERY);
        s_fault_common_seen |= bit;
        s_fault_common_software |= bit;
    } else {
        fault_offer_notice(code, fault_severity(code));
    }
}

void PowerAlgo_ExtLoadTimeout(void) {
    uint8_t inhibit = ext_inhibit(Millis());
    if (inhibit & EXT_INHIBIT_PM)
        return;
    if (s_fault_offline || ext_fault_code(inhibit)) {
        PowerAlgo_ExtLoadReportBlocked();
        return;
    }
    if (!s_fault_meter_timeout) {
        fault_report(40u, FAULT_METER);
        s_fault_meter_timeout = 1u;
    }
}

void PowerAlgo_GetFaultView(power_fault_view_t *out) {
    if (!out)
        return;
    *out = s_fault;
    out->active = 0u;
    out->valid = 1u;
    uint32_t now = Millis();
    switch (s_fault_source) {
    case FAULT_SW:
        if (!s_fault.code)
            break;
        out->valid = fault_status_fresh(now);
        out->active = (s_fault_prev_live & s_fault_live_mask) != 0u;
        if (s_fault.code == 4u && ext_sample_fresh(EXT_NTC, now) && s_ext_hot)
            out->active = 1u;
        if (s_fault.code == 1u && (ext_inhibit(now) & EXT_INHIBIT_EMPTY))
            out->active = 1u;
        break;
    case FAULT_NTC:
        out->valid = SW6306_IsInitialized() && ext_sample_fresh(EXT_NTC, now);
        out->active = s_ext_hot;
        break;
    case FAULT_BATTERY:
        out->valid = fault_status_fresh(now);
        uint8_t inhibit = ext_inhibit(now);
        out->active = ext_fault_code(inhibit & EXT_INHIBIT_FAULT) == s_fault.code;
        if (s_fault.code == 41u || s_fault.code == 1u) {
            out->valid &= ext_sample_fresh(EXT_ADC, now) && ext_sample_fresh(EXT_CAPACITY, now);
            out->active |= (inhibit & EXT_INHIBIT_EMPTY) != 0u;
        }
        break;
    case FAULT_OFFLINE:
        out->active = s_fault_offline;
        break;
    case FAULT_WLED:
        out->active = WLED_AlgoThermalBlocked();
        break;
    default:
        /* Non-latched AUX / meter errors are history after shutdown. */
        break;
    }
}

void PowerAlgo_UpdateFaultItems(ui_t *ui) {
    if (ui && (!ui->nowItem ||
               (ui->nowItem->page.location != s_fault_items[0].page.location &&
                ui->nowItem->page.jumpPage != s_fault_items[0].page.location)))
        return;
    power_fault_view_t view;
    PowerAlgo_GetFaultView(&view);
    if (!view.code)
        (void)mini_snprintf(s_fault_labels[0], sizeof(s_fault_labels[0]), " NO FAULT");
    else
        (void)mini_snprintf(s_fault_labels[0], sizeof(s_fault_labels[0]), " E%02u %s", (unsigned)view.code,
                            !view.valid ? "UNKNOWN" : view.active ? "ACTIVE"
                                                                  : "HISTORY");
    (void)mini_snprintf(s_fault_labels[1], sizeof(s_fault_labels[1]), " %s", view.code ? PowerAlgo_NoticeText(view.code) : "--");
    (void)mini_snprintf(s_fault_labels[2], sizeof(s_fault_labels[2]), " Count: %u", (unsigned)view.count);
}

void PowerAlgo_AddFaultItems(ui_page_t *page) {
    page->wideText = 1u;
    PowerAlgo_UpdateFaultItems(NULL);
    for (unsigned i = 0; i < 3u; ++i)
        AddItem(s_fault_labels[i], UI_ITEM_DATA, 0, &s_fault_items[i], page, 0, 0);
}

void PowerAlgo_OnSharedIRQ(void) {
    s_meter_irq = 1u;
}

static uint8_t meter_ports_fresh(uint32_t now) {
    return s_meter_ports_valid && (uint32_t)(now - s_meter_ports_at) <= SW_MONITOR_STALE_MS;
}

static uint8_t meter_ready(uint32_t now) {
    return !s_bus_force_off && meter_ports_fresh(now) &&
           (s_meter_real_path || (!s_meter_real_present && s_meter_a2 == METER_A2_ON));
}

uint8_t PowerAlgo_ExtLoadRequest(uint8_t load) {
    /* Missing startup telemetry may settle inside the caller's wait window.
     * A confirmed cutoff is never queued for automatic restart. */
    if (ext_inhibit(Millis()) & ~EXT_INHIBIT_DATA) {
        PowerAlgo_ExtLoadReportBlocked();
        return 0u;
    }
    if (!ext_loads())
        s_fault_meter_timeout = 0u;
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

uint8_t PowerAlgo_GetExtLoadTemperature(int16_t *c10) {
    *c10 = s_monitor.ntc_c10;
    return SW6306_IsInitialized() && ext_sample_fresh(EXT_NTC, Millis());
}

void PowerAlgo_ReportWLEDOverheat(void) {
    fault_report(30u, FAULT_WLED);
}

static void meter_note_ports(uint32_t now) {
    uint8_t connections = SW6306_ConnectedPortMask();
    uint8_t a2_on = SW6306_IsPortA2ON() != 0u;
    uint8_t a2_online = (connections & SW6306_CONNECTED_A2) != 0u;
    uint8_t was_present = s_meter_real_present;
    s_meter_a1_on = SW6306_IsPortA1ON() != 0u;
    uint8_t c1_path = SW6306_IsPortC1ON() || SW6306_IsCharging();
    s_meter_real_path = c1_path || s_meter_a1_on;
    s_meter_real_present = connections & (SW6306_CONNECTED_C1 | SW6306_CONNECTED_A1);
    if (c1_path)
        s_meter_real_present |= SW6306_CONNECTED_C1;
    if (s_meter_a1_on)
        s_meter_real_present |= SW6306_CONNECTED_A1;
    s_meter_ports_at = now;
    s_meter_ports_valid = 1u;

    /* A2 is exclusive to firmware, so any observed ON path can be reused.
     * While a command is pending, confirm its latched direction or wait
     * until the existing retry interval expires before changing state. */
    meter_a2_state_t observed = a2_on ? METER_A2_ON : a2_online ? METER_A2_UNKNOWN
                                                                : METER_A2_OFF;
    if (s_meter_a2 != METER_A2_PENDING ||
        observed == (s_a2_target ? METER_A2_ON : METER_A2_OFF) ||
        (uint32_t)(now - s_meter_command_at) >= METER_RETRY_MS)
        s_meter_a2 = observed;

    /* Real USB insertion wins immediately, including CC detection before
     * a C1 power path exists. Do not wait for the normal retry slot. */
    if ((s_meter_real_present & (uint8_t)~was_present) && s_meter_a2 != METER_A2_OFF)
        s_meter_attempt_at = now - METER_RETRY_MS;
}

static void meter_invalidate(void) {
    s_meter_ports_valid = 0u;
    s_meter_real_present = s_meter_real_path = 0u;
    s_meter_a2 = METER_A2_UNKNOWN;
}

static uint8_t meter_remove_due(uint32_t now) {
    if (!meter_ports_fresh(now) || s_meter_a2 == METER_A2_OFF ||
        (s_meter_a2 == METER_A2_PENDING && !s_a2_target))
        return 0u;
    if (s_meter_real_present)
        return 1u;
    if (s_meter_a2 == METER_A2_PENDING)
        return 0u;
    if (!ext_loads())
        return (uint32_t)(now - s_ext_release_at) >= METER_RELEASE_MS;
    return s_meter_a2 == METER_A2_UNKNOWN && !ext_inhibit(now);
}

static uint8_t meter_urgent_due(uint32_t now) {
    return s_meter_real_present && meter_remove_due(now) &&
           (uint32_t)(now - s_meter_attempt_at) >= METER_RETRY_MS;
}

static uint8_t meter_due(uint32_t now) {
    if ((uint32_t)(now - s_meter_attempt_at) < METER_RETRY_MS)
        return 0u;
    if (!meter_ports_fresh(now))
        return s_ext_pm_work || ext_loads() || s_meter_a2 != METER_A2_OFF;
    if (meter_remove_due(now))
        return 1u;
    return ext_loads() && !ext_inhibit(now) &&
           !s_meter_real_present && s_meter_a2 == METER_A2_OFF;
}

/* A synthetic path alone is not a discharge session. While charging,
 * mixed external loads have no unambiguous net discharge direction. */
static uint8_t battery_discharging(uint32_t now) {
    return !SW6306_IsCharging() && meter_ports_fresh(now) &&
           ((s_meter_real_path && SW6306_IsDischarging()) ||
            (s_ext_active && meter_ready(now)));
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

static void battery_publish_lifetime(void);

static void sw6306_display_read_failed(void) {
    sw6306_bus_read_failed();
    s_monitor.ntc_valid = 0u;
    s_power_data[POWER_BUS_VOLTAGE].ptr = NULL;
    s_power_data[POWER_BUS_CURRENT].ptr = NULL;
    s_power_data[POWER_CHIP_TEMP].ptr = NULL;
    for (unsigned i = 0; i < BATTERY_SOH; ++i)
        s_battery_data[i].ptr = NULL;
    BatteryAlgo_InvalidSample();
    battery_publish_lifetime();
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
        BatteryAlgo_Finalize();
        battery_publish_lifetime();
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
    meter_invalidate();
    BatteryAlgo_InvalidSample();
    battery_publish_lifetime();
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
        PowerAlgo_ExtLoadReportBlocked();
    } else if (s_aux_state == AUX_STATE_WAIT_METER) {
        if (state == EXT_LOAD_READY)
            aux_start();
        else if ((uint32_t)(Millis() - s_aux_wait_at) >= EXT_LOAD_WAIT_MS) {
            aux_disable();
            PowerAlgo_ExtLoadTimeout();
        }
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
    if (!s_sw_owner_busy && s_meter_a2 == METER_A2_OFF && meter_ports_fresh(Millis())) {
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
        s_fault_status_ok = 0u;
        s_ext_valid = 0u;
        s_meter_ports_valid = 0u;
        BatteryAlgo_InvalidSample();
        battery_publish_lifetime();
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
        fault_report(20u, FAULT_AUX);
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
    static const char *const names[POWER_ITEM_COUNT] = {" BUS Voltage V", " BUS Current A", " Aux-A Voltage V", " Aux-A Current A", " Chip Temperature°C"};
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

static uint32_t battery_round_div(uint32_t value, uint32_t divisor) {
    return value / divisor + (value % divisor >= divisor / 2u);
}

static void battery_publish_lifetime(void) {
    static const char *const learn_names[] = {"WAIT", "LEARN", "DONE", "--"};
    battery_view_t view;
    BatteryAlgo_GetView(&view);
    s_battery_values[BATTERY_SOH] = view.soh_d10;
    s_battery_data[BATTERY_SOH].ptr = view.full_mwh ? &s_battery_values[BATTERY_SOH] : NULL;

    /* Round before choosing the width, so 9.9999 becomes 10.00,
     * rather than an overflowing six-character 10.000. */
    uint32_t divisor = 10u;
    uint8_t decimals = 3u;
    while (decimals && battery_round_div(view.efc_q10000, divisor) >= 10000u) {
        divisor *= 10u;
        --decimals;
    }
    uint32_t value = battery_round_div(view.efc_q10000, divisor);
    s_battery_data[BATTERY_EFC].decimals = decimals;
    s_battery_values[BATTERY_EFC] = (int)value;
    s_battery_data[BATTERY_EFC].dataType = value > 99999u ? UI_DATA_STRING : UI_DATA_FLOAT;
    s_battery_data[BATTERY_EFC].ptr = value > 99999u ? (void *)">99k" : &s_battery_values[BATTERY_EFC];
    s_battery_data[BATTERY_LEARN].ptr = (void *)learn_names[view.learn];
}

void PowerAlgo_AddBatteryItems(ui_page_t *page) {
    static const char *const names[BATTERY_ITEM_COUNT] = {
        " Charge Level %", " Battery Voltage V", " Full Energy Wh", " Remaining Energy Wh", " Battery Temperature°C",
        " State of Health %", " Equivalent Full Cycles", " Capacity Learning"};
    for (unsigned i = 0; i < BATTERY_ITEM_COUNT; ++i) {
        s_battery_data[i].name = names[i];
        s_battery_data[i].dataType = i == BATTERY_LEARN ? UI_DATA_STRING : i == BATTERY_CHARGE ? UI_DATA_INT
                                                                                               : UI_DATA_FLOAT;
        s_battery_data[i].actionType = UI_DATA_ACTION_RO;
        s_battery_elements[i].data = &s_battery_data[i];
        Create_element(&s_battery_items[i], &s_battery_elements[i]);
        AddItem(names[i], UI_ITEM_DATA, 0, &s_battery_items[i], page, 0, 0);
    }
    s_battery_data[BATTERY_VOLTAGE].decimals = 2u;
    s_battery_data[BATTERY_TEMP].decimals = 1u;
    s_battery_data[BATTERY_SOH].decimals = 1u;
    battery_publish_lifetime();
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

static void battery_sample(uint8_t valid) {
    if (valid) {
        uint32_t now = Millis();
        uint8_t direction_valid = s_status_fresh && meter_ports_fresh(now);
        if (direction_valid && SW6306_IsCharging() && s_ext_active) {
            /* Mixed charging/external load has no reliable net direction.
             * Settle through the last discharge sample, then pause EFC. */
            BatteryAlgo_Finalize();
            direction_valid = 0u;
        }
        if (BatteryAlgo_Update(s_full_mwh, s_remain_mwh, SW6306_ReadLearnState(),
                               direction_valid, battery_discharging(now)))
            fault_offer_notice(NOTICE_LEARN_DONE, POWER_NOTICE_INFO);
    } else {
        BatteryAlgo_InvalidSample();
    }
    battery_publish_lifetime();
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
    s_meter_irq = 0u; /* An IRQ arriving during this read stays pending. */
    before = SW6306_PortReadSequence();
    SW6306_OperationReset();
    THRD_SPAWN_NOARG(SW6306_PortStatusLoad);
    if (SW6306_LastOperationOK() && SW6306_PortReadSequence() != before) {
        meter_note_ports(Millis());
        sw6306_connection_update();
    } else {
        s_meter_ports_valid = 0u;
        if (s_meter_a2 != METER_A2_PENDING)
            s_meter_a2 = METER_A2_UNKNOWN;
        sw6306_bus_read_failed();
    }
    THRD_END;
}

static void meter_update_guard(void) {
    s_idle_verified = meter_ports_fresh(Millis()) &&
                      s_meter_a2 == METER_A2_OFF && SW6306_TransportPortsIdle();
    s_monitor.port_busy = !s_idle_verified;
    pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK, !s_idle_verified);
    if (!s_idle_verified && !s_ext_pm_closing)
        pm_api_refresh_idle();
}

THRD_DECLARE(meter_remove) {
    THRD_BEGIN;
    s_meter_attempt_at = Millis();
    s_a2_target = 0u;
    s_meter_a2 = METER_A2_UNKNOWN;
    SW6306_OperationReset();
    THRD_SPAWN_NOARG(SW6306_PortA2Remove);
    s_meter_command_at = Millis();
    if (SW6306_LastOperationOK())
        s_meter_a2 = METER_A2_PENDING;
    THRD_SPAWN_NOARG(meter_read_ports);
    meter_update_guard();
    THRD_END;
}

THRD_DECLARE(meter_step) {
    THRD_BEGIN;
    /* The urgent path uses the just-completed physical port snapshot:
     * no additional port read or phase delay before A2 Remove. */
    if (!meter_urgent_due(Millis())) {
        s_meter_attempt_at = Millis();
        THRD_SPAWN_NOARG(meter_read_ports);
    }
    if (!s_meter_ports_valid) {
        meter_update_guard();
        PT_EXIT(pt);
    }

    if (meter_remove_due(Millis())) {
        THRD_SPAWN_NOARG(meter_remove);
    } else if (ext_loads() && !ext_inhibit(Millis()) &&
               !s_meter_real_present && s_meter_a2 == METER_A2_OFF) {
        s_meter_attempt_at = Millis();
        s_a2_target = 1u;
        s_meter_a2 = METER_A2_UNKNOWN;
        SW6306_OperationReset();
        THRD_SPAWN_NOARG(SW6306_PortA2Insert);
        s_meter_command_at = Millis();
        if (SW6306_LastOperationOK())
            s_meter_a2 = METER_A2_PENDING;
        THRD_SPAWN_NOARG(meter_read_ports);
        /* An A1/C1 insertion can race the asynchronous insert. */
        if (meter_urgent_due(Millis()))
            THRD_SPAWN_NOARG(meter_remove);
    }
    meter_update_guard();
    THRD_END;
}

/* PB5 is shared with SC7A20: the edge is a read hint, never proof of
 * USB insertion. A2 also gets a 100 ms periodic fallback poll. */
THRD_DECLARE(meter_poll) {
    THRD_BEGIN;
    THRD_SPAWN_NOARG(meter_read_ports);
    if (meter_urgent_due(Millis()))
        THRD_SPAWN_NOARG(meter_remove);
    meter_update_guard();
    THRD_END;
}

THRD_DECLARE(battery_learn_rearm) {
    THRD_BEGIN;
    /* The DONE sample has already updated/saved capacity. Acknowledge its
     * event before rearming, so a new completion cannot be cleared later. */
    SW6306_OperationReset();
    THRD_SPAWN_ARGS(SW6306_ClearEvents, SW6306_FAULT0_LEARNEND);
    if (!SW6306_LastOperationOK())
        PT_EXIT(pt);
    SW6306_OperationReset();
    THRD_SPAWN_NOARG(SW6306_CapacityLearningRearm);
    if (SW6306_LastOperationOK())
        BatteryAlgo_LearnRearmed();
    THRD_END;
}

/* Only the SW6306 owner consumes dirty bits. One register operation or
 * power transaction per service slot leaves the monitor and ADC responsive. */
static uint8_t config_ports_idle(uint32_t now) {
    return s_idle_verified && meter_ports_fresh(now) && SW6306_TransportPortsIdle() &&
           !ext_loads() && s_meter_a2 == METER_A2_OFF;
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
        ext_loads() || s_meter_a2 != METER_A2_OFF ||
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
            if (SW6306_IsInitialized()) {
                s_sw_owner_busy = 1u;
                /* Keep reading while a command is pending; PM cannot
                 * accept an old OFF state or an unconfirmed removal. */
                THRD_SPAWN_NOARG(meter_poll);
                if (meter_due(Millis()))
                    THRD_SPAWN_NOARG(meter_step);
                s_sw_owner_busy = 0u;
            }
            THRD_DELAY(50u);
            continue;
        }
        if(!SW6306_IsInitialized()) {
            if (s_fault_has_snapshot || s_monitor.initialized)
                fault_offline();
            s_fault_status_ok = 0u;
            s_ext_valid = 0u;
            meter_invalidate();
            BatteryAlgo_Invalidate();
            battery_publish_lifetime();
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
                if (s_fault_init_failed)
                    fault_offline();
                s_fault_init_failed = 1u;
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
            s_fault_init_failed = 0u;
            config_restore_switches();
            idle_reset();
            s_port_baseline_valid=0u; /* first successful PortStatusLoad */
            s_sw_next_at = Millis() + 100u;
            s_scene_seen = 0u;
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
            if (!SW6306_IsInitialized())
                fault_offline();
            THRD_DELAY(100u);
            continue;
        }

        if(s_bg_radio_reads_allowed) {
            /* No regular 5-phase scanner in UI_OFF. The inspection
             * explicitly requests a fresh ADC + Capacity pair above. */
            THRD_DELAY(20u);
            continue;
        }
        /* Service PB5 edges between scheduled phases. Do not shorten
         * the normal ADC / capacity scan cadence on sensor interrupts. */
        if (s_meter_irq) {
            s_sw_owner_busy = 1u;
            THRD_SPAWN_NOARG(meter_poll);
            s_sw_owner_busy = 0u;
        }
        if ((int32_t)(Millis() - s_sw_next_at) < 0) {
            THRD_UNTIL((int32_t)(Millis() - s_sw_next_at) >= 0 ||
                       s_meter_irq || s_ext_pm_work || pm_api_ui_should_block());
            continue;
        }
        if (s_sw_phase != 2u && s_meter_a2 != METER_A2_OFF) {
            s_sw_owner_busy = 1u;
            THRD_SPAWN_NOARG(meter_poll);
            s_sw_owner_busy = 0u;
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
            s_status_fresh=(uint8_t)(SW6306_LastOperationOK() &&
                SW6306_StatusReadSequence()!=s_sw_seq_before);
            if (s_status_fresh)
                fault_consume_status();
            else
                s_fault_status_ok = 0u;
            sw6306_cache_ntc();
            if (s_status_fresh && !(ext_inhibit(Millis()) & EXT_INHIBIT_DATA)) {
                uint8_t inhibit = ext_inhibit(Millis());
                uint16_t active = fault_common_bit(ext_fault_code(inhibit));
                if (inhibit & EXT_INHIBIT_HOT)
                    active |= fault_common_bit(4u);
                if (inhibit & EXT_INHIBIT_EMPTY)
                    active |= fault_common_bit(41u) | fault_common_bit(1u);
                if (inhibit & EXT_INHIBIT_FAULT)
                    active |= fault_common_bit(ext_fault_code(EXT_INHIBIT_FAULT));
                s_fault_common_seen &= active;
                s_fault_common_software &= active;
            }
            if (s_status_fresh && s_fault_clear_mask) {
                SW6306_OperationReset();
                THRD_SPAWN_ARGS(SW6306_ClearEvents, s_fault_clear_mask);
                if (SW6306_LastOperationOK())
                    s_fault_seen_events &= (uint8_t)~s_fault_clear_mask;
            }
            s_sw_owner_busy = 0u;
        }
        else if(s_sw_phase==2u) {
            s_sw_owner_busy = 1u;
            THRD_SPAWN_NOARG(meter_poll);
            if (s_meter_ports_valid && s_scene_seen) {
                SW6306_OperationReset();
                THRD_SPAWN_ARGS(SW6306_ClearEvents, SW6306_FAULT0_SCENE);
                if (SW6306_LastOperationOK())
                    s_scene_seen = 0u;
            }
            s_sw_owner_busy = 0u;
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
                meter_invalidate();
                s_idle_verified = 0u;
                s_bus_force_off = 0u;
            } else if (BatteryAlgo_LearnRearmPending()) {
                s_sw_owner_busy = 1u;
                THRD_SPAWN_NOARG(battery_learn_rearm);
                s_sw_owner_busy = 0u;
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
                meter_invalidate();
                BatteryAlgo_Invalidate();
                battery_publish_lifetime();
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
                battery_sample(s_monitor.capacity_valid);
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
                s_monitor.discharging = battery_discharging(Millis());
                s_monitor.protocol = SW6306_ReadProtocol();
                s_monitor.sample_ms = Millis();
                sw6306_update_eta(s_monitor.sample_ms);
            }
            s_status_fresh=0u;
        }
        /* Phase 3 uses the idle I2C slot only when the meter has work. */
        s_sw_phase=(uint8_t)((s_sw_phase+1u)%5u);
        s_sw_next_at = Millis() + 100u;
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
    BatteryAlgo_Init();
    battery_publish_lifetime();
    meter_invalidate();
    aux_disable();
    s_monitor.protocol="--";
    s_sw_phase=0u;s_capacity_div=1u;s_idle_verified=0u;
    s_adc_fresh = s_eta_mode = s_bus_display_valid = 0u;
    s_port_baseline_valid=0u;s_previous_connections=0u;
    s_status_fresh=0u;
    s_retry_at=Millis(); /* first service tick attempts SW6306_Init */
    s_sw_next_at = Millis();
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
