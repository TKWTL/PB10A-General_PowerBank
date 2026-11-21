/* PB10A FM algorithm: one coroutine-owned state machine and station list.
 * The only path to RDA5807 hardware is the nonblocking BSP API.
 * UI submits commands/reads a RAM snapshot; PM may request an async stop.
 * Collar-Radio 87..108 MHz, 100 kHz tuning and 50 sorted presets retained.
 */
#include "radio_algo.h"
#include "rda5807m.h"
#include "pm_device.h"
#include "pm_api.h"
#include "i2c.h"
#include "millis.h"
#include "main.h"
#include "power_algo.h"
#include <string.h>

#define RADIO_PM_BLOCK       0x04u
#define RADIO_QUERY_MS       180u
#define RADIO_SEEK_TIMEOUT_MS 8000u
#define RADIO_COMMAND_QUEUE  8u
#define RADIO_DEFAULT_VOLUME 0u

static uint8_t s_state,s_mode=RADIO_MODE_PRESET,s_power_mode=RADIO_POWER_AUTO;
static uint8_t s_volume,s_rssi,s_stereo,s_search_failed,s_error_code;
static uint8_t s_stop_pending,s_stop_requested,s_start_requested,s_pm_stopping;
static uint8_t s_boot_sleep_ack,s_full_scan,s_first_seek_pending,s_station_count;
static uint16_t s_freq_units=3480u,s_scan_last_freq;
static uint16_t s_stations[RADIO_MAX_STATIONS];
static uint32_t s_state_at,s_last_query;
static uint8_t s_view_open,s_battery_inspection_pending,s_low_battery_latched;
static uint32_t s_last_battery_guard;
static radio_command_t s_commands[RADIO_COMMAND_QUEUE];
static uint8_t s_cmd_head,s_cmd_count;

/* All operations below only queue BSP requests; no blocking I2C here. */
static void radio_fail(uint8_t why) {
    s_state=RADIO_STATE_ERROR;
    s_error_code=why;
    s_full_scan=0u;s_start_requested=0u;
    s_boot_sleep_ack=0u;
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_NOCLOCK,LL_RCC_MCO1_DIV_1);
    pm_api_set_sleep_block(RADIO_PM_BLOCK,0u);
}
/* One common cutoff path for normal RUN monitoring and RTC inspections.
 * Once empty, do not let a stale button command restart the radio until
 * fresh charge telemetry has cleared the low-battery latch. */
static void radio_force_low_battery_off(void) {
    s_low_battery_latched=1u;
    s_power_mode=RADIO_POWER_OFF;
    s_cmd_count=0u;
    s_start_requested=0u;
    s_stop_requested=1u;
}
static void radio_start(void) {
    if(s_low_battery_latched || s_state!=RADIO_STATE_OFF ||
        s_stop_pending || s_pm_stopping)return;
    RDA5807_AsyncResetStatus();
    s_error_code=0u;s_search_failed=0u;s_first_seek_pending=1u;
    s_full_scan=0u;s_scan_last_freq=0u;
    s_volume=RADIO_DEFAULT_VOLUME;s_stereo=0u;s_rssi=0u;
    s_freq_units=3480u;s_station_count=0u;
    /* Active FM is not an idle-timer block. Its async operations are
     * drained by radio_prepare before STOP1; MCO must keep running. */
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_LSE,LL_RCC_MCO1_DIV_1);
    if(RDA5807_AsyncReset()!=I2C_OK) {
        radio_fail(1u);return;
    }
    s_state=RADIO_STATE_RESET_WAIT;
    s_state_at=Millis();
}
static void radio_stop(void) {
    if(s_state!=RADIO_STATE_OFF && !s_stop_pending) {
        LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_LSE,LL_RCC_MCO1_DIV_1);
        if(RDA5807_AsyncSleep()!=I2C_OK)return; /* retry when queue drains */
        s_stop_pending=1u;
    }
    s_first_seek_pending=1u;s_full_scan=0u;
    s_state=RADIO_STATE_OFF;
    s_stop_requested=0u;
}
static void radio_seek(uint8_t upward) {
    if(RDA5807_AsyncSeek(upward)!=I2C_OK){radio_fail(4u);return;}
    s_state=RADIO_STATE_SEARCH;
    s_state_at=Millis();s_last_query=s_state_at;
    s_search_failed=0u;s_first_seek_pending=0u;
}
static void radio_tune(uint16_t freq) {
    if(s_state!=RADIO_STATE_READY && s_state!=RADIO_STATE_SEARCH &&
        s_state!=RADIO_STATE_TUNE_WAIT)return;
    if(freq<3480u)freq=4320u;
    if(freq>4320u)freq=3480u;
    if(RDA5807_AsyncTune(freq)!=I2C_OK){radio_fail(4u);return;}
    s_freq_units=freq;
    s_state=RADIO_STATE_TUNE_WAIT;
    s_state_at=Millis();s_last_query=s_state_at-RADIO_QUERY_MS;
    s_first_seek_pending=0u;
}
static void radio_volume_step(uint8_t up) {
    uint8_t v;
    if(s_state!=RADIO_STATE_READY)return;
    v=up?(s_volume<15u?s_volume+1u:15u):
        (s_volume>0u?s_volume-1u:0u);
    if(v==s_volume)return;
    if(RDA5807_AsyncSetVolume(v)!=I2C_OK){radio_fail(4u);return;}
    s_volume=v;
}
static void radio_save_station(uint16_t freq) {
    uint8_t i,j;
    if(s_station_count>=RADIO_MAX_STATIONS)return;
    for(i=0u;i<s_station_count && s_stations[i]<freq;i++){}
    if(i<s_station_count && s_stations[i]==freq)return;
    for(j=s_station_count;j>i;j--)s_stations[j]=s_stations[j-1u];
    s_stations[i]=freq;
    ++s_station_count;
}
static void radio_toggle_station(uint16_t freq) {
    uint8_t i,j;
    for(i=0u;i<s_station_count;i++) {
        if(s_stations[i]==freq) {
            for(j=i;j+1u<s_station_count;j++)
                s_stations[j]=s_stations[j+1u];
            --s_station_count;
            return;
        }
    }
    radio_save_station(freq);
}
static void radio_move_station(uint8_t up) {
    uint8_t i;
    if(!s_station_count) {
        s_station_count=0u;
        s_full_scan=1u;
        s_scan_last_freq=0u;
        radio_tune(3480u);
        s_first_seek_pending=1u;
        return;
    }
    if(s_full_scan) {
        s_full_scan=0u; /* navigating a preset cancels global seek */
        radio_tune(s_freq_units);
        return;
    }
    if(up) {
        for(i=0u;i<s_station_count;i++)
            if(s_stations[i]>s_freq_units){radio_tune(s_stations[i]);return;}
        radio_tune(s_stations[0]);
    }
    else {
        for(i=s_station_count;i>0u;i--)
            if(s_stations[i-1u]<s_freq_units) {
                radio_tune(s_stations[i-1u]);return;
            }
        radio_tune(s_stations[s_station_count-1u]);
    }
}
static void radio_start_full_scan(void) {
    if(s_state==RADIO_STATE_OFF || s_state==RADIO_STATE_ERROR)return;
    s_station_count=0u;s_full_scan=1u;s_scan_last_freq=0u;
    radio_tune(3480u);
    s_first_seek_pending=1u;
}
static void radio_frequency_step(uint8_t up) {
    if(s_state!=RADIO_STATE_READY && s_state!=RADIO_STATE_SEARCH)return;
    if(s_mode==RADIO_MODE_FREQ) {
        if(s_state==RADIO_STATE_SEARCH)radio_tune(s_freq_units);
        else radio_tune(up?(s_freq_units>=4320u?3480u:s_freq_units+4u):
            (s_freq_units<=3480u?4320u:s_freq_units-4u));
    }
    else if(s_mode==RADIO_MODE_PRESET)radio_move_station(up);
    else if(s_state==RADIO_STATE_SEARCH)radio_tune(s_freq_units);
    else radio_seek(up);
}
static void radio_handle_command(radio_command_t cmd) {
    switch(cmd) {
    case RADIO_CMD_VIEW_ENTER:
        s_view_open=1u;
        if(s_power_mode!=RADIO_POWER_OFF) {
            if(s_stop_pending)s_start_requested=1u;
            else if(s_state==RADIO_STATE_OFF)radio_start();
        }
        break;
    case RADIO_CMD_VIEW_EXIT:
        s_view_open=0u;
        if(s_power_mode!=RADIO_POWER_ON)s_stop_requested=1u;
        break;
    case RADIO_CMD_VOLUME_UP:radio_volume_step(1u);break;
    case RADIO_CMD_VOLUME_DOWN:radio_volume_step(0u);break;
    case RADIO_CMD_FREQUENCY_UP:radio_frequency_step(1u);break;
    case RADIO_CMD_FREQUENCY_DOWN:radio_frequency_step(0u);break;
    case RADIO_CMD_MODE_CYCLE:
        s_mode=(uint8_t)((s_mode+1u)%3u);
        s_full_scan=0u;
        break;
    case RADIO_CMD_POWER_CYCLE:
        if(s_low_battery_latched) {
            s_power_mode=RADIO_POWER_OFF;
            break;
        }
        /* Collar-Radio cycles ON -> AUTO -> OFF -> ON.
         * Starting from default AUTO, first ENTER selects OFF. */
        s_power_mode=s_power_mode==RADIO_POWER_OFF?RADIO_POWER_ON:
            s_power_mode==RADIO_POWER_ON?RADIO_POWER_AUTO:RADIO_POWER_OFF;
        if(s_power_mode==RADIO_POWER_OFF) {
            s_start_requested=0u;
            s_stop_requested=1u;
        }
        else if((s_power_mode==RADIO_POWER_ON || s_view_open) &&
            s_state==RADIO_STATE_OFF) {
            if(s_stop_pending)s_start_requested=1u;
            else radio_start();
        }
        break;
    case RADIO_CMD_TOGGLE_STATION:
        if(s_mode==RADIO_MODE_PRESET)radio_start_full_scan();
        else radio_toggle_station(s_freq_units);
        break;
    case RADIO_CMD_SCAN_ALL:radio_start_full_scan();break;
    default:break;
    }
}

uint8_t RadioAlgo_Request(radio_command_t cmd) {
    uint8_t tail;
    if((unsigned)cmd>RADIO_CMD_SCAN_ALL || s_cmd_count>=RADIO_COMMAND_QUEUE)
        return 0u;
    tail=(uint8_t)((s_cmd_head+s_cmd_count)%RADIO_COMMAND_QUEUE);
    s_commands[tail]=cmd;
    ++s_cmd_count;
    return 1u;
}
void RadioAlgo_GetSnapshot(radio_snapshot_t *out) {
    if(!out)return;
    out->state=s_state;out->mode=s_mode;out->power_mode=s_power_mode;
    out->volume=s_volume;out->rssi=s_rssi;out->stereo=s_stereo;
    out->searching=(uint8_t)(s_state==RADIO_STATE_SEARCH);
    out->search_failed=s_search_failed;
    out->station_count=s_station_count;out->error_code=s_error_code;
    out->frequency=s_freq_units;
    memcpy(out->stations,s_stations,sizeof(s_stations));
}
void RadioAlgo_Stop(void) {
    s_stop_requested=1u;
    s_start_requested=0u;
}
uint8_t RadioAlgo_StopPending(void) {
    return (uint8_t)(s_stop_requested||s_stop_pending||
        s_state!=RADIO_STATE_OFF||RDA5807_AsyncBusy());
}
uint8_t RadioAlgo_BootSleepAcknowledged(void) {
    return (uint8_t)(s_boot_sleep_ack && !s_stop_pending &&
        !RDA5807_AsyncBusy() && s_state==RADIO_STATE_OFF);
}
uint8_t RadioAlgo_BackgroundWakeNeeded(void) {
    return (uint8_t)(s_power_mode==RADIO_POWER_ON &&
        (s_state==RADIO_STATE_READY || s_state==RADIO_STATE_SEARCH ||
         s_state==RADIO_STATE_TUNE_WAIT) &&
        !s_stop_pending && !s_stop_requested);
}
void RadioAlgo_RequestBatteryInspection(void) {
    if(RadioAlgo_BackgroundWakeNeeded()) {
        s_battery_inspection_pending=1u;
        PowerAlgo_RequestRadioBatteryCheck();
    }
}
static pm_hook_result_t radio_prepare(void *ctx) {
    (void)ctx;
    if(!s_pm_stopping) {
        /* A stale UI command must never reactivate FM while entering STOP1. */
        s_cmd_count=0u;s_start_requested=0u;
    }
    s_pm_stopping=1u;
    /* Power Mode ON is allowed to continue receiving through STOP1.
     * Only wait for in-flight I2C; do NOT enqueue ENABLE=0 or stop MCO. */
    if(RadioAlgo_BackgroundWakeNeeded())
        return RDA5807_AsyncBusy()?PM_HOOK_PENDING:PM_HOOK_DONE;
    if(RadioAlgo_StopPending())RadioAlgo_Stop();
    return RadioAlgo_StopPending()?PM_HOOK_PENDING:PM_HOOK_DONE;
}
static pm_hook_result_t radio_resume(void *ctx) {
    (void)ctx;
    /* SystemClock_Config() restores MCO=LSE unconditionally after STOP1.
     * Reapply the Radio ownership policy in the reverse-order PM resume:
     * keep clock for active FM, otherwise stop driving the output. */
    if(RadioAlgo_BackgroundWakeNeeded())
        LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_LSE,LL_RCC_MCO1_DIV_1);
    else if(!s_stop_pending)
        LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_NOCLOCK,LL_RCC_MCO1_DIV_1);
    s_pm_stopping=0u;
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
    "rda5807",radio_prepare,0,radio_resume,0,110u,0,0
};
void RadioAlgo_Init(void) {
    s_state=RADIO_STATE_OFF;
    s_mode=RADIO_MODE_PRESET;s_power_mode=RADIO_POWER_AUTO;
    s_stop_pending=0u;s_stop_requested=0u;s_start_requested=0u;
    s_cmd_head=s_cmd_count=0u;
    s_station_count=0u;s_view_open=0u;s_full_scan=0u;
    s_battery_inspection_pending=0u;
    s_low_battery_latched=0u;s_last_battery_guard=Millis();
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_LSE,LL_RCC_MCO1_DIV_1);
    s_boot_sleep_ack=(uint8_t)(RDA5807_AsyncBootSleep()==I2C_OK);
    s_stop_pending=s_boot_sleep_ack;
    if(!s_boot_sleep_ack)
        LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_NOCLOCK,LL_RCC_MCO1_DIV_1);
    pm_api_set_sleep_block(RADIO_PM_BLOCK,s_boot_sleep_ack);
    (void)pm_device_register(&s_node);
}

/* The dedicated radio coroutine calls this at 5 ms. All queued I2C
 * writes and status reads settle by IRQ while other coroutines run. */
void RadioAlgo_Poll(void) {
    uint32_t now=Millis();
    uint8_t ret;
    uint16_t status=0u,signal=0u;
    RDA5807_AsyncPoll();
    if(s_stop_pending) {
        if(RDA5807_AsyncBusy())return;
        s_stop_pending=0u;
        LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_NOCLOCK,LL_RCC_MCO1_DIV_1);
        pm_api_set_sleep_block(RADIO_PM_BLOCK,0u);
    }
    if(RDA5807_AsyncTakeError()) {
        radio_fail(4u);
        if(s_pm_stopping)s_stop_requested=1u;
        return;
    }
    if(s_stop_requested) {
        radio_stop();
        if(s_stop_requested)return;
        if(s_stop_pending)return;
    }
    if(s_pm_stopping)return;
    /* A receiver running with the UI on also benefits from the same
     * cutoff. The 500 ms check reads SW6306's existing RAM mirror only. */
    if((uint32_t)(now-s_last_battery_guard)>=500u) {
        power_monitor_t power;
        s_last_battery_guard=now;
        PowerAlgo_GetMonitor(&power);
        if(power.valid && power.capacity_valid) {
            if(power.capacity_pct==0u || power.vbat_mv<14000u)
                radio_force_low_battery_off();
            else if(power.capacity_pct>5u && power.vbat_mv>=14500u)
                s_low_battery_latched=0u;
        }
    }
    if(s_battery_inspection_pending) {
        uint16_t battery_mv=0u;
        uint8_t battery_pct=0u;
        uint8_t checked=PowerAlgo_TakeRadioBatteryCheck(&battery_mv,&battery_pct);
        if(checked!=0u) {
            s_battery_inspection_pending=0u;
            /* Inspect only complete fresh SW6306 transactions. Never
             * infer empty capacity from a communications failure. */
            if(checked==1u &&
                (battery_pct==0u || battery_mv<14000u)) {
                /* Mandatory low battery shutdown: enforce OFF in the
                 * algorithm-owned mirror so the Radio UI displays OFF.
                 * Clear old UI commands to prevent automatic restart. */
                radio_force_low_battery_off();
            }
        }
    }
    if(s_start_requested && s_state==RADIO_STATE_OFF) {
        s_start_requested=0u;
        radio_start();
    }
    if(s_cmd_count) {
        radio_command_t cmd=s_commands[s_cmd_head];
        s_cmd_head=(uint8_t)((s_cmd_head+1u)%RADIO_COMMAND_QUEUE);
        --s_cmd_count;
        radio_handle_command(cmd);
    }
    if(s_state==RADIO_STATE_OFF || s_state==RADIO_STATE_ERROR)return;
    if(s_state==RADIO_STATE_RESET_WAIT) {
        if((uint32_t)(now-s_state_at)<50u)return;
        if(RDA5807_AsyncPowerUp(s_volume)!=I2C_OK)
            {radio_fail(2u);return;}
        s_state=RADIO_STATE_POWER_WAIT;s_state_at=now;
        return;
    }
    if(s_state==RADIO_STATE_POWER_WAIT) {
        if((uint32_t)(now-s_state_at)<400u)return;
        s_state=RADIO_STATE_TUNE_WAIT;
        s_state_at=now;s_last_query=now-RADIO_QUERY_MS;
        return;
    }
    if(s_state!=RADIO_STATE_TUNE_WAIT && s_state!=RADIO_STATE_SEARCH &&
        s_state!=RADIO_STATE_READY)return;
    if((uint32_t)(now-s_last_query)<RADIO_QUERY_MS)return;
    s_last_query=now;
    ret=RDA5807_AsyncTakeStatus(&status,&signal);
    if(ret==RDA5807_ASYNC_PENDING)return;
    if(ret!=I2C_OK){radio_fail(5u);return;}
    s_rssi=(uint8_t)((signal&RDA5807_REG0B_RSSI_MSK)>>9);
    s_stereo=(uint8_t)((status&RDA5807_REG0A_ST)!=0u);
    s_freq_units=(uint16_t)(3480u+4u*(status&RDA5807_REG0A_CHAN_MSK));
    if(s_state==RADIO_STATE_TUNE_WAIT) {
        if((status&RDA5807_REG0A_STC) ||
            (uint32_t)(now-s_state_at)>=1200u) {
            if(s_first_seek_pending)radio_seek(1u);
            else s_state=RADIO_STATE_READY;
        }
    }
    else if(s_state==RADIO_STATE_SEARCH) {
        if((status&RDA5807_REG0A_STC) &&
            (uint32_t)(now-s_state_at)>=250u) {
            s_search_failed=(uint8_t)((status&RDA5807_REG0A_SF)!=0u);
            if(!s_search_failed)radio_save_station(s_freq_units);
            s_state=RADIO_STATE_READY;
            if(s_full_scan) {
                if(s_search_failed || s_station_count>=RADIO_MAX_STATIONS ||
                    s_freq_units>=4320u ||
                    (s_scan_last_freq && s_freq_units<=s_scan_last_freq))
                    s_full_scan=0u;
                else {
                    s_scan_last_freq=s_freq_units;
                    radio_seek(1u);
                }
            }
        }
        else if((uint32_t)(now-s_state_at)>RADIO_SEEK_TIMEOUT_MS) {
            s_full_scan=0u;radio_fail(6u);
        }
    }
}
