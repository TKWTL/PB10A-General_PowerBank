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
static uint32_t s_retry_at;

static uint8_t s_sw_low_power;
static pm_hook_result_t sw_prepare(void *ctx)
{
 (void)ctx;
 return PM_HOOK_DONE; /* UI_OFF does not stop SW6306 */
}
static pm_hook_result_t sw_suspend(void *ctx)
{
    (void)ctx;
    if(s_sw_low_power)return PM_HOOK_DONE;
    /* Never enter STOP if chip is offline, any port is active, or I2C
     * cannot verify the live CC/port status. This is a fresh read, not
     * the potentially stale 500ms cache. */
    if(!SW6306_IsInitialized() || !SW6306_TransportPortsIdle())
    {
        s_idle_verified=0u;
        pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u);
        return PM_HOOK_ERROR;
    }
    s_idle_verified=1u;
    SW6306_LPSet();
    s_sw_low_power=1u;
    return PM_HOOK_DONE;
}
static pm_hook_result_t sw_resume(void *ctx)
{
    (void)ctx;
    if(!s_sw_low_power)return PM_HOOK_DONE;
    if(!SW6306_WakeAndUnlock())return PM_HOOK_ERROR;
    s_sw_low_power=0u;
    s_idle_verified=0u; /* next service cycle rechecks live connections */
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
 "sw6306",sw_prepare,sw_suspend,sw_resume,0,90u,0,0
};


/* Auxiliary A-port PM and ADC1 DMA sampling, merged from power_adc_algo. */
#define POWER_ADC_TIMEOUT_MS 50u

/* DMA writes four 16-bit ADC results; static storage stays alive across IRQs. */
static uint16_t s_dma[4] __attribute__((aligned(4)));
/* 0 = pending, 1 = transfer completed, 2 = ADC/DMA error callback. */
static volatile uint8_t s_transfer_result;
static uint8_t s_active, s_suspended, s_cal_failed;
static uint32_t s_started_at;
static int s_pa0_raw, s_pa5_raw, s_usb_mv;
static int s_adc_state; /* 0 OK,1 cal,2 start,3 timeout,4 callback,5 pending */
static int s_adc_hal_error, s_dma_hal_error;
static ui_item_t s_pa0_item,s_pa5_item,s_mv_item,s_state_item;
static ui_item_t s_adc_hal_item,s_dma_hal_item;
static ui_data_t s_data[6];
static ui_element_t s_elements[6];

static pm_hook_result_t power_adc_suspend(void *ctx)
{
    HAL_StatusTypeDef result;
    (void)ctx;
    s_suspended=1u;
    /* Remove auxiliary A-port power only after ADC/DMA is stopped. */
    if(s_active)
    {
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
static pm_hook_result_t power_adc_resume(void *ctx)
{
    (void)ctx;
    /* STM32 clock/tick restoration precedes this callback. */
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_suspended=0u;
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_power_adc_pm={
    "power-adc",0,power_adc_suspend,power_adc_resume,0,200u,0,0
};

static void power_adc_init(void)
{
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_active=0u;s_suspended=0u;s_transfer_result=0u;
    s_pa0_raw=0;s_pa5_raw=0;s_usb_mv=0;
    s_adc_state=5;s_adc_hal_error=0;s_dma_hal_error=0;
    s_cal_failed=(HAL_ADCEx_Calibration_Start(&hadc1)!=HAL_OK)?1u:0u;
    if(s_cal_failed)
    {
        s_adc_state=1;
        s_adc_hal_error=(int)HAL_ADC_GetError(&hadc1);
    }
}
uint8_t PowerAlgo_AdcIsSuspended(void){return s_suspended;}

uint8_t PowerAlgo_AdcBeginScan(void)
{
    HAL_StatusTypeDef result;
    if(s_suspended)return 0u;
    s_transfer_result=0u;
    /* Keep the previous scan's error snapshot until this scan settles. */
    s_active=1u; /* Set BEFORE start: callback may run immediately. */
    s_started_at=Millis();
    result=HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)s_dma,4u);
    if(result!=HAL_OK)
    {
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
uint8_t PowerAlgo_AdcScanSettled(void)
{
    uint8_t result;
    if(s_suspended || !s_active)return 1u;
    result=s_transfer_result;
    if(result==0u && (uint32_t)(Millis()-s_started_at)<=POWER_ADC_TIMEOUT_MS)
        return 0u;

    if(result!=1u)
    {
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
    if(result==1u)
    {
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

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc && hadc->Instance==ADC1 && s_active)
        s_transfer_result=1u;
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc && hadc->Instance==ADC1 && s_active)
        s_transfer_result=2u;
}

void PowerAlgo_AddStatisticsItems(ui_page_t *page)
{
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
    for(i=0u;i<6u;i++)
    {
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



/* SW6306 read-only monitor policy. One group per 100ms, yielding between
 * groups; avoid dumping every I2C transaction into a single UI frame.
 * Initialization is attempted on the first service tick and then every 5s
 * if the driver is uninitialized. SW6306_Init() itself uses legacy IRQ-I2C
 * synchronous waits: it is NOT a truly asynchronous coroutine operation. */
void PowerAlgo_Service100ms(void)
{
    uint32_t now=Millis();
    if(pm_api_ui_should_block())return;
    if(!SW6306_IsInitialized())
    {
        s_monitor.initialized=0u;
        s_monitor.valid=0u;
        s_monitor.capacity_valid=0u;
        s_idle_verified=0u;
        pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u); /* fail safe */
        if((int32_t)(now-s_retry_at)<0)return;
        s_retry_at=now+SW_REINIT_DELAY_MS;
        SW6306_Init(); /* PB02B driver configuration, one attempt per retry */
        if(!SW6306_IsInitialized())return;
        s_sw_phase=0u;s_capacity_div=1u;
        s_status_fresh=0u;s_sw_low_power=0u;
        s_monitor.initialized=1u;
        return;
    }

    /* Successful refreshes populate a single RAM-only UI snapshot. */
    switch(s_sw_phase)
    {
    case 0u:
        SW6306_ADCLoad(); /* BUS and battery voltage/current */
        break;
    case 1u:
    {
        uint32_t before=SW6306_StatusReadSequence();
        SW6306_StatusLoad(); /* charge/discharge direction */
        s_status_fresh=(uint8_t)(SW6306_StatusReadSequence()!=before);
        break;
    }
    case 2u:
        SW6306_PortStatusLoad(); /* C1/A1 live connection and port paths */
        s_idle_verified=SW6306_TransportPortsIdle();
        s_monitor.port_busy=(uint8_t)!s_idle_verified;
        pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,!s_idle_verified);
        if(!s_idle_verified)pm_api_refresh_idle();
        break;
    case 3u:
        break; /* preserve PB02B's port -> power spacing */
    default:
        SW6306_PowerLoad(); /* fast-charge protocol and limits */
        if(++s_capacity_div>=2u)
        {
            s_capacity_div=0u;
            SW6306_CapacityLoad();
            if(SW6306_CapacityLoadOK())
            {
                s_monitor.capacity_pct=SW6306_ReadCapacity();
                if(s_monitor.capacity_pct>100u)s_monitor.capacity_pct=100u;
                s_monitor.capacity_valid=1u;
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
        s_monitor.sample_ms=now;
        /* StatusLoad succeeded in this 500ms cycle. Legacy ADCLoad
         * and PowerLoad return void, so per-group freshness is unknown. */
        s_monitor.valid=s_status_fresh;
        s_status_fresh=0u;
        break;
    }
    s_sw_phase=(uint8_t)((s_sw_phase+1u)%5u);
}

void PowerAlgo_GetMonitor(power_monitor_t *out)
{
    if(!out)return;
    *out=s_monitor; /* foreground coroOS only; no interrupt writer */
    if(!SW6306_IsInitialized())out->initialized=out->valid=0u;
    if((uint32_t)(Millis()-out->sample_ms)>SW_MONITOR_STALE_MS)
        out->valid=0u;
}

void PowerAlgo_Init(void)
{
    power_adc_init(); /* assert PA6 and calibrate ADC1 */
    memset(&s_monitor,0,sizeof(s_monitor));
    s_monitor.protocol="--";
    s_sw_phase=0u;s_capacity_div=1u;s_idle_verified=0u;
    s_status_fresh=0u;
    s_retry_at=Millis(); /* first service tick attempts SW6306_Init */
    s_sw_low_power=0u;
    pm_api_set_sleep_block(POWER_SW_SLEEP_BLOCK,1u); /* fail closed until idle proven */
    (void)pm_device_register(&s_node);         /* SW6306 / order 90 */
    (void)pm_device_register(&s_power_adc_pm); /* SUB_EN + ADC / order 200 */
}
