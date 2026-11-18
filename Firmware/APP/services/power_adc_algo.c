/* PB10A auxiliary USB-A ADC sampled by a dedicated coroOS protothread.
 *
 * Wiring: PA0 = SUB_IOUT, PA5 = SUB_VOUT, PA6 = SUB_EN (held HIGH).
 * CH0, CH5, temperature, VREFINT form one four-rank software-triggered
 * sequence. ADC1 DMA must be configured NORMAL (not circular) in CubeMX;
 * continuous conversion and DMA continuous requests must both stay disabled.
 *
 * Each coroOS cycle starts exactly one ADC/DMA transaction, then yields
 * until the DMA completion/error callback or a timeout. Sampling cadence
 * comes from THRD_DELAY(500) in app_task.c, NOT from this driver.
 *
 * Power manager: suspend this driver BEFORE STOP1; resume AFTER clock restore.
 * UI_OFF does not suspend ADC. PA6 is not altered on suspend/resume.
 */
#include "power_adc_algo.h"
#include "adc.h"
#include "main.h"
#include "millis.h"
#include "core/ui.h"
#include "pm_device.h"

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

static void power_adc_suspend(void *ctx)
{
    (void)ctx;
    s_suspended=1u;
    if(s_active)
    {
        (void)HAL_ADC_Stop_DMA(&hadc1);
        s_active=0u;
        s_transfer_result=0u;
    }
}
static void power_adc_resume(void *ctx)
{
    (void)ctx;
    /* STM32 platform node (order 250) restores clocks first. */
    s_suspended=0u;
}
static const pm_device_node_t s_power_adc_pm={
    "power-adc",0,power_adc_suspend,power_adc_resume,0,200u,0,0
};

void PowerADC_Init(void)
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
    /* Register before pm_api_init; pm_device maintains nodes by order.
     * ADC node suspends before STM32 clock/tick node (order 250). */
    (void)pm_device_register(&s_power_adc_pm);
}
uint8_t PowerADC_IsSuspended(void){return s_suspended;}

uint8_t PowerADC_BeginScan(void)
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
uint8_t PowerADC_ScanSettled(void)
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

void PowerADC_AddStatisticsItems(ui_page_t *page)
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
