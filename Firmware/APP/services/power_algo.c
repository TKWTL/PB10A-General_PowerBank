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
#include <string.h>

#define POWER_SW_SLEEP_BLOCK 0x08u /* separate from scanner (0x02) and radio (0x04) */
#define SW_REINIT_DELAY_MS 5000u
#define SW_MONITOR_STALE_MS 2500u
static power_monitor_t s_monitor;
static uint8_t s_sw_phase,s_capacity_div,s_idle_verified,s_status_fresh;
static uint8_t s_sw_owner_busy,s_cycle_ok;
static uint32_t s_sw_seq_before;
static uint32_t s_retry_at;

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
/* Auxiliary A-port PM and ADC1 DMA sampling, merged from power_adc_algo. */
#define POWER_ADC_TIMEOUT_MS 50u

/* DMA writes four 16-bit ADC results; static storage stays alive across IRQs. */
static uint16_t s_dma[4] __attribute__((aligned(4)));
/* 0 = pending, 1 = transfer completed, 2 = ADC/DMA error callback. */
static volatile uint8_t s_transfer_result;
static uint8_t s_active, s_suspended, s_cal_failed;
static uint32_t s_started_at;
static int s_pa0_raw, s_pa5_raw, s_usb_mv;
static int s_vdda_mv,s_die_temp_c;
static uint8_t s_system_adc_valid;
static int s_adc_state; /* 0 OK,1 cal,2 start,3 timeout,4 callback,5 pending */
static int s_adc_hal_error, s_dma_hal_error;
static ui_item_t s_pa0_item,s_pa5_item,s_mv_item,s_state_item;
static ui_item_t s_adc_hal_item,s_dma_hal_item;
static ui_data_t s_data[6];
static ui_element_t s_elements[6];

static pm_hook_result_t power_adc_suspend(void *ctx) {
    HAL_StatusTypeDef result;
    (void)ctx;
    s_suspended=1u;
    /* Remove auxiliary A-port power only after ADC/DMA is stopped. */
    if(s_active) {
        result=HAL_ADC_Stop_DMA(&hadc1);
        s_active=0u;
        s_transfer_result=0u;
    }
    else
        result=HAL_ADC_Stop(&hadc1);
    if(result!=HAL_OK)return PM_HOOK_ERROR;
    LL_GPIO_ResetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    return PM_HOOK_DONE;
}

static pm_hook_result_t power_adc_resume(void *ctx) {
    (void)ctx;
    /* STM32 clock/tick restoration precedes this callback. */
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_suspended=0u;
    return PM_HOOK_DONE;
}

static const pm_device_node_t s_power_adc_pm={
    "power-adc",0,power_adc_suspend,power_adc_resume,0,200u,0,0
};

static void power_adc_init(void) {
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_active=0u;s_suspended=0u;s_transfer_result=0u;
    s_pa0_raw=0;s_pa5_raw=0;s_usb_mv=0;
    s_vdda_mv=0;s_die_temp_c=0;s_system_adc_valid=0u;
    s_adc_state=5;s_adc_hal_error=0;s_dma_hal_error=0;
    s_cal_failed=(HAL_ADCEx_Calibration_Start(&hadc1)!=HAL_OK)?1u:0u;
    if(s_cal_failed) {
        s_adc_state=1;
        s_adc_hal_error=(int)HAL_ADC_GetError(&hadc1);
    }
}

uint8_t PowerAlgo_AdcIsSuspended(void){return s_suspended;}

uint8_t PowerAlgo_AdcBeginScan(void) {
    HAL_StatusTypeDef result;
    if(s_suspended)return 0u;
    s_transfer_result=0u;
    /* Keep the previous scan's error snapshot until this scan settles. */
    s_active=1u; /* Set BEFORE start: callback may run immediately. */
    s_started_at=Millis();
    result=HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)s_dma,4u);
    if(result!=HAL_OK) {
        s_adc_hal_error=(int)HAL_ADC_GetError(&hadc1);
        s_dma_hal_error=(int)HAL_DMA_GetError(hadc1.DMA_Handle);
        s_adc_state=2;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        s_active=0u;
        return 0u;
    }
    return 1u;
}

/* Called by ADC coroutine while active; 0 => yield, 1 => this scan done.
 * Neither spin-waits nor starts another conversion. */
uint8_t PowerAlgo_AdcScanSettled(void) {
    uint8_t result;
    if(s_suspended || !s_active)return 1u;
    result=s_transfer_result;
    if(result==0u && (uint32_t)(Millis()-s_started_at)<=POWER_ADC_TIMEOUT_MS)
        return 0u;

    if(result!=1u) {
        s_system_adc_valid=0u;
        s_adc_hal_error=(int)HAL_ADC_GetError(&hadc1);
        s_dma_hal_error=(int)HAL_DMA_GetError(hadc1.DMA_Handle);
    }
    /* A completed NORMAL DMA transfer is already idle: do not stop and
     * disable the ADC after a successful one-shot sequence. The next
     * HAL_ADC_Start_DMA() re-arms DMA for the following 500ms sample.
     * Abort only on a failed or timed-out sequence. */
    if(result!=1u)
        (void)HAL_ADC_Stop_DMA(&hadc1);
    s_active=0u;
    if(result==1u) {
        /* Factory VREFINT and temperature calibration, rank 4 then rank 3. */
        if(s_dma[3]) {
            s_vdda_mv=(int)__HAL_ADC_CALC_VREFANALOG_VOLTAGE(s_dma[3],ADC_RESOLUTION_12B);
            s_die_temp_c=(int)__HAL_ADC_CALC_TEMPERATURE((uint32_t)s_vdda_mv,s_dma[2],ADC_RESOLUTION_12B);
            s_system_adc_valid=1u;
        }
        else s_system_adc_valid=0u;
        s_pa0_raw=(int)s_dma[0];
        s_pa5_raw=(int)s_dma[1];
        /* Vout_mV = raw*3300/4095*(680+220)/220,
         * equivalent to raw * 148500 / 45045; fits uint32_t. */
        s_usb_mv=(int)(((uint32_t)s_dma[1]*148500u+22522u)/45045u);
        s_adc_hal_error=0;
        s_dma_hal_error=0;
        s_adc_state=s_cal_failed?1:0;
    }
    else if(result==2u)
        s_adc_state=4;
    else
        s_adc_state=3;
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

void PowerAlgo_AddStatisticsItems(ui_page_t *page) {
    static ui_item_t *const items[6]={
        &s_pa0_item,&s_pa5_item,&s_mv_item,&s_state_item,
        &s_adc_hal_item,&s_dma_hal_item
    };
    static int *const values[6]={
        &s_pa0_raw,&s_pa5_raw,&s_usb_mv,&s_adc_state,
        &s_adc_hal_error,&s_dma_hal_error
    };
    static const char *const names[6]={
        " PA0 I raw"," PA5 V raw"," Vout~ mV",
        " ADC Err"," ADC HAL"," DMA HAL"
    };
    uint8_t i;
    if(!page)return;
    for(i=0u;i<6u;i++) {
        s_data[i].name=names[i];
        s_data[i].ptr=values[i];
        s_data[i].dataType=UI_DATA_INT;
        s_data[i].actionType=UI_DATA_ACTION_RO;
        s_data[i].function=0;
        s_data[i].decimals=0u;
        s_elements[i].data=&s_data[i];
        Create_element(items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,items[i],page,0,0);
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

/**************************** 服务注册与初始化 ***************************/
void PowerAlgo_Init(void) {
    power_adc_init(); /* assert PA6 and calibrate ADC1 */
    memset(&s_monitor,0,sizeof(s_monitor));
    s_monitor.protocol="--";
    s_sw_phase=0u;s_capacity_div=1u;s_idle_verified=0u;
    s_status_fresh=0u;
    s_retry_at=Millis(); /* first service tick attempts SW6306_Init */
    s_sw_low_power=0u;s_sw_owner_busy=0u;
    s_sw_suspend_stage=0u;s_sw_resume_started=0u;
    s_cycle_ok=0u;
    pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u); /* fail closed until idle proven */
    (void)pm_device_register(&s_node);         /* SW6306 / order 90 */
    (void)pm_device_register(&s_power_adc_pm); /* SUB_EN + ADC / order 200 */
}

void PowerAlgo_GetSystemAdc(int *vdda_mv,int *die_temp_c,uint8_t *valid) {
    if(vdda_mv)*vdda_mv=s_vdda_mv;
    if(die_temp_c)*die_temp_c=s_die_temp_c;
    if(valid)*valid=(uint8_t)(s_system_adc_valid && !s_suspended);
}
