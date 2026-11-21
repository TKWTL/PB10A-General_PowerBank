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
static power_monitor_t s_monitor;
static uint8_t s_sw_phase,s_capacity_div,s_idle_verified,s_status_fresh;
static uint8_t s_sw_owner_busy,s_cycle_ok;
static uint32_t s_sw_seq_before;
static uint32_t s_retry_at;
static uint8_t s_port_baseline_valid,s_previous_connections;
/* Independent request/result mailbox, single owner: PowerAlgo_SW6306Thread. */
static uint8_t s_radio_battery_check_request,s_radio_battery_check_result;
static uint8_t s_radio_battery_adc_ok,s_radio_battery_pct;
static uint16_t s_radio_battery_mv;

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
 * If a transition is aborted while I2C is active, finish its HAL transfer
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
    if(!SW6306_LastOperationOK()) {
        SW6306_Invalidate();
        return PM_HOOK_ERROR;
    }
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_node={
    "sw6306",sw_prepare,sw_suspend,sw_resume,0,90u,0,0
};

/************************* 附加 A 口与 ADC1 DMA *************************/
/* ADC rank order: PA0 current, PA5 divided voltage, die temperature,
 * VREFINT. DMA completion only signals the cooperative owner thread. */
#define POWER_ADC_TIMEOUT_MS 50u
#define AUX_A_VDIV_HIGH 680u
#define AUX_A_VDIV_LOW  220u
#define AUX_A_CURRENT_MV_PER_A 500u

static uint16_t s_dma[4] __attribute__((aligned(4)));
static volatile uint8_t s_transfer_result; /* 0 pending, 1 done, 2 error */
static uint8_t s_active,s_suspended,s_adc_initialized;
static uint8_t s_aux_inserted,s_aux_enabled,s_system_adc_valid;
static uint32_t s_started_at;
static int s_aux_voltage_mv,s_aux_current_ma;
static int s_vdda_mv,s_die_temp_c10;
static ui_item_t s_aux_voltage_item,s_aux_current_item;
static ui_data_t s_aux_data[2];
static ui_element_t s_aux_elements[2];

static pm_hook_result_t power_adc_suspend(void *ctx) {
    HAL_StatusTypeDef result;
    (void)ctx;
    s_suspended=1u;
    if(s_active) {
        result=HAL_ADC_Stop_DMA(&hadc1);
        s_active=0u;
        s_transfer_result=0u;
    }
    else
        result=HAL_ADC_Stop(&hadc1);
    if(result!=HAL_OK)return PM_HOOK_ERROR;
    LL_GPIO_ResetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_aux_enabled=0u;
    s_system_adc_valid=0u;
    return PM_HOOK_DONE;
}

static pm_hook_result_t power_adc_resume(void *ctx) {
    (void)ctx;
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_aux_enabled=1u;
    s_suspended=0u;
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_power_adc_pm={
    "power-adc",0,power_adc_suspend,power_adc_resume,0,200u,0,0
};

/* Invoked once by ADC_RTC_AuxAThread before its for(;;). */
void PowerAlgo_AdcInit(void) {
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_active=0u;s_suspended=0u;s_transfer_result=0u;
    s_aux_enabled=1u;s_aux_inserted=0u;
    s_aux_voltage_mv=0;s_aux_current_ma=0;
    s_vdda_mv=0;s_die_temp_c10=0;s_system_adc_valid=0u;
    s_adc_initialized=(uint8_t)(HAL_ADCEx_Calibration_Start(&hadc1)==HAL_OK);
}

uint8_t PowerAlgo_AdcIsSuspended(void){return s_suspended;}

uint8_t PowerAlgo_AdcBeginScan(void) {
    if(s_suspended || !s_adc_initialized)return 0u;
    s_transfer_result=0u;
    s_active=1u; /* Set before HAL start: IRQ completion can be immediate. */
    s_started_at=Millis();
    if(HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)s_dma,4u)!=HAL_OK) {
        s_active=0u;
        s_system_adc_valid=0u;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        return 0u;
    }
    return 1u;
}

/* The cooperative owner waits for IRQ completion and calculates all values.
 * No fixed 3300 mV assumption: rank 4 VREFINT determines measured VDDA. */
uint8_t PowerAlgo_AdcScanSettled(void) {
    uint32_t pin_voltage_mv,pin_current_mv;
    int32_t cal_span,scaled_temp10;
    uint8_t result;
    if(s_suspended || !s_active)return 1u;
    result=s_transfer_result;
    if(result==0u && (uint32_t)(Millis()-s_started_at)<POWER_ADC_TIMEOUT_MS)
        return 0u;
    s_active=0u;
    if(result!=1u) {
        (void)HAL_ADC_Stop_DMA(&hadc1);
        s_system_adc_valid=0u;
        return 1u;
    }
    if(s_dma[3]==0u) {
        s_system_adc_valid=0u;
        return 1u;
    }
    s_vdda_mv=(int)__HAL_ADC_CALC_VREFANALOG_VOLTAGE(s_dma[3],ADC_RESOLUTION_12B);
    pin_current_mv=((uint32_t)s_dma[0]*(uint32_t)s_vdda_mv+2047u)/4095u;
    pin_voltage_mv=((uint32_t)s_dma[1]*(uint32_t)s_vdda_mv+2047u)/4095u;
    /* PA0: 0.5 V/A (500 mV/A). PA5: 680 k / 220 k divider. */
    s_aux_current_ma=(int)(pin_current_mv*1000u/AUX_A_CURRENT_MV_PER_A);
    s_aux_voltage_mv=(int)((pin_voltage_mv*
        (AUX_A_VDIV_HIGH+AUX_A_VDIV_LOW)+AUX_A_VDIV_LOW/2u)/AUX_A_VDIV_LOW);

    /* The HAL integer helper truncates to whole degrees. Use the same
     * factory TS_CAL1/TS_CAL2 values but retain a tenth-degree digit. */
    cal_span=(int32_t)*TEMPSENSOR_CAL2_ADDR-(int32_t)*TEMPSENSOR_CAL1_ADDR;
    if(cal_span!=0) {
        scaled_temp10=(int32_t)(((uint32_t)s_dma[2]*(uint32_t)s_vdda_mv*10u+
            TEMPSENSOR_CAL_VREFANALOG/2u)/TEMPSENSOR_CAL_VREFANALOG);
        s_die_temp_c10=(int)(TEMPSENSOR_CAL1_TEMP*10+
            (scaled_temp10-(int32_t)*TEMPSENSOR_CAL1_ADDR*10)*
            (TEMPSENSOR_CAL2_TEMP-TEMPSENSOR_CAL1_TEMP)/cal_span);
        s_system_adc_valid=1u;
    }
    else s_system_adc_valid=0u;
    return 1u;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
    if(hadc && hadc->Instance==ADC1 && s_active)
        s_transfer_result=1u;
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
    if(hadc && hadc->Instance==ADC1 && s_active)
        s_transfer_result=2u;
}

/* PA7 is pulled up: low indicates an inserted auxiliary-A load.
 * The existing RUN/UI_OFF -> STOP1 enable policy is unchanged. */
void PowerAlgo_AuxA_Service(void) {
    s_aux_inserted=(uint8_t)!LL_GPIO_IsInputPinSet(SUB_INDET_GPIO_Port,SUB_INDET_Pin);
    if(s_suspended)return;
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_aux_enabled=1u;
}

uint8_t PowerAlgo_AuxAIsInserted(void){return s_aux_inserted;}
uint8_t PowerAlgo_AuxAIsEnabled(void){return s_aux_enabled;}

void PowerAlgo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[2]={" Aux-A V mV"," Aux-A I mA"};
    int *values[2]={&s_aux_voltage_mv,&s_aux_current_ma};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<2u;i++) {
        s_aux_data[i].name=names[i];
        s_aux_data[i].ptr=values[i];
        s_aux_data[i].dataType=UI_DATA_INT;
        s_aux_data[i].actionType=UI_DATA_ACTION_RO;
        s_aux_data[i].function=0;
        s_aux_data[i].decimals=0u;
        s_aux_elements[i].data=&s_aux_data[i];
        Create_element(i==0u?&s_aux_voltage_item:&s_aux_current_item,&s_aux_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,
            i==0u?&s_aux_voltage_item:&s_aux_current_item,page,0,0);
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
        if(s_sw_low_power || pm_api_ui_should_block()) {
            THRD_DELAY(20u);
            continue;
        }
        if(!SW6306_IsInitialized()) {
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
            s_radio_battery_adc_ok=SW6306_LastOperationOK();
            if(s_radio_battery_adc_ok)
                s_radio_battery_mv=SW6306_ReadVBAT();
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_CapacityLoad);
            s_sw_owner_busy=0u;
            if(s_radio_battery_adc_ok && SW6306_LastOperationOK() &&
                SW6306_CapacityLoadOK()) {
                s_radio_battery_pct=SW6306_ReadCapacity();
                if(s_radio_battery_pct>100u)s_radio_battery_pct=100u;
                s_monitor.vbat_mv=s_radio_battery_mv;
                s_monitor.capacity_pct=s_radio_battery_pct;
                s_monitor.capacity_valid=1u;
                s_radio_battery_check_result=1u;
            }
            else s_radio_battery_check_result=2u;
            THRD_DELAY(100u);
            continue;
        }

        if(s_sw_phase==0u) {
            s_cycle_ok=1u;
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_ADCLoad);
            s_sw_owner_busy=0u;
            if(!SW6306_LastOperationOK())s_cycle_ok=0u;
        }
        else if(s_sw_phase==1u) {
            s_sw_seq_before=SW6306_StatusReadSequence();
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_StatusLoad);
            s_sw_owner_busy=0u;
            s_status_fresh=(uint8_t)(SW6306_LastOperationOK() &&
                SW6306_StatusReadSequence()!=s_sw_seq_before);
            if(!s_status_fresh)s_cycle_ok=0u;
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
            if(!SW6306_LastOperationOK())s_cycle_ok=0u;
            pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,!s_idle_verified);
            if(!s_idle_verified)pm_api_refresh_idle();
        }
        else if(s_sw_phase==4u) {
            s_sw_owner_busy=1u;
            SW6306_OperationReset();
            THRD_SPAWN_NOARG(SW6306_PowerLoad);
            s_sw_owner_busy=0u;
            if(!SW6306_LastOperationOK())s_cycle_ok=0u;
            if(++s_capacity_div>=2u) {
                s_capacity_div=0u;
                s_sw_owner_busy=1u;
                SW6306_OperationReset();
                THRD_SPAWN_NOARG(SW6306_CapacityLoad);
                s_sw_owner_busy=0u;
                s_monitor.capacity_valid=(uint8_t)(SW6306_LastOperationOK() && SW6306_CapacityLoadOK());
                if(s_monitor.capacity_valid) {
                    s_monitor.capacity_pct=SW6306_ReadCapacity();
                    if(s_monitor.capacity_pct>100u)s_monitor.capacity_pct=100u;
                }
            }
            s_monitor.initialized=1u;
            s_monitor.vbus_mv=SW6306_ReadVBUS();
            s_monitor.ibus_ma=SW6306_ReadIBUS();
            s_monitor.vbat_mv=SW6306_ReadVBAT();
            s_monitor.ibat_ma=SW6306_ReadIBAT();
            s_monitor.c1_on=SW6306_IsPortC1ON()?1u:0u;
            s_monitor.a1_on=SW6306_IsPortA1ON()?1u:0u;
            s_monitor.charging=SW6306_IsCharging()?1u:0u;
            s_monitor.discharging=SW6306_IsDischarging()?1u:0u;
            s_monitor.protocol=SW6306_ReadProtocol();
            s_monitor.sample_ms=Millis();
            s_monitor.valid=(uint8_t)(s_status_fresh && s_cycle_ok);
            s_status_fresh=0u;
        }
        /* Phase 3 intentionally leaves an idle gap on the shared bus. */
        s_sw_phase=(uint8_t)((s_sw_phase+1u)%5u);
        THRD_DELAY(100u);
    }
    THRD_END;
}

void PowerAlgo_GetMonitor(power_monitor_t *out) {
    if(!out)return;
    *out=s_monitor; /* foreground coroOS only; no interrupt writer */
    if(!SW6306_IsInitialized())out->initialized=out->valid=0u;
    if((uint32_t)(Millis()-out->sample_ms)>SW_MONITOR_STALE_MS)
        out->valid=0u;
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
    s_monitor.protocol="--";
    s_sw_phase=0u;s_capacity_div=1u;s_idle_verified=0u;
    s_port_baseline_valid=0u;s_previous_connections=0u;
    s_status_fresh=0u;
    s_retry_at=Millis(); /* first service tick attempts SW6306_Init */
    s_sw_low_power=0u;s_sw_owner_busy=0u;
    s_sw_suspend_stage=0u;s_sw_resume_started=0u;
    s_cycle_ok=0u;
    s_radio_battery_check_request=0u;
    s_radio_battery_check_result=0u;
    s_radio_battery_adc_ok=0u;
    pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u); /* fail closed until idle proven */
    (void)pm_device_register(&s_node);         /* SW6306 / order 90 */
    (void)pm_device_register(&s_power_adc_pm); /* SUB_EN + ADC / order 200 */
}

void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c10,uint8_t *valid) {
    if(vdda_mv)*vdda_mv=s_vdda_mv;
    if(die_temp_c10)*die_temp_c10=s_die_temp_c10;
    if(valid)*valid=(uint8_t)(s_system_adc_valid && !s_suspended);
}
