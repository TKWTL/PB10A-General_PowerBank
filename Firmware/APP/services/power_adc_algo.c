/* PB10A auxiliary USB-A feedback, independent from temporary hw_test.
 * Hardware/Preview/Netlist_主板.tel:
 *   PA6 SUB_EN is the independent power circuit enable (kept high);
 *   PA0 SUB_IOUT = INA199A1 output;
 *   PA5 SUB_VOUT = divider R39=680k high, R41=220k low.
 *
 * ADC1 currently scans CH0, CH5, temperature, VREFINT. For this build the
 * CubeMX DMA configuration is circular but ADC itself is non-continuous,
 * so software explicitly starts one four-rank sequence every 200ms.
 *
 * Calibration and DMA-start errors are recorded SEPARATELY. Even if
 * calibration fails, a raw DMA capture is attempted for diagnosis.
 * Approx USB-A voltage assumes Vdda=3.3V; no claim of absolute accuracy.
 */
#include "power_adc_algo.h"
#include "adc.h"
#include "main.h"
#include "millis.h"
#include "core/ui.h"

#define POWER_ADC_PERIOD_MS 200u
#define POWER_ADC_TIMEOUT_MS 35u

static uint16_t s_dma[4] __attribute__((aligned(4)));
static volatile uint8_t s_complete, s_dma_error;
static uint8_t s_running, s_cal_error, s_fault;
static uint32_t s_start_at, s_last_at;
static int s_pa0_raw, s_pa5_raw, s_usb_mv;
static int s_adc_state; /* 0=OK,1=Cal failure,2=Start failure,3=Timeout,4=DMA error,5=waiting */
static ui_item_t s_pa0_item,s_pa5_item,s_mv_item,s_status_item;
static ui_data_t s_data[4];
static ui_element_t s_elements[4];

static void power_adc_start(uint32_t now)
{
    s_complete=0u;
    s_dma_error=0u;
    s_last_at=now;
    if(HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)s_dma,4u)!=HAL_OK)
    {
        s_fault=2u;
        s_adc_state=2;
        (void)HAL_ADC_Stop_DMA(&hadc1);
        return;
    }
    s_running=1u;
    s_start_at=now;
}

void PowerADC_Init(void)
{
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    s_running=0u;s_complete=0u;s_dma_error=0u;
    s_fault=0u;s_pa0_raw=0;s_pa5_raw=0;s_usb_mv=0;
    s_adc_state=5;
    s_cal_error=(HAL_ADCEx_Calibration_Start(&hadc1)!=HAL_OK)?1u:0u;
    power_adc_start(Millis());
}

void PowerADC_Poll(void)
{
    uint32_t now=Millis();
    /* Do not disable PA6 when an ADC transfer fails or completes. */
    if(s_running)
    {
        if(s_complete)
        {
            /* Stop DMA before copying the four halfword samples. */
            (void)HAL_ADC_Stop_DMA(&hadc1);
            s_running=0u;
            s_pa0_raw=(int)s_dma[0];
            s_pa5_raw=(int)s_dma[1];
            /* Vout_mV = raw*3300/4095*(680+220)/220.
             * raw*148500 <= 608107500, safe within uint32_t. */
            s_usb_mv=(int)(((uint32_t)s_dma[1]*148500u+22522u)/45045u);
            s_fault=0u;
            s_adc_state=s_cal_error?1:0;
        }
        else if(s_dma_error || (uint32_t)(now-s_start_at)>POWER_ADC_TIMEOUT_MS)
        {
            (void)HAL_ADC_Stop_DMA(&hadc1);
            s_running=0u;
            s_fault=s_dma_error?4u:3u;
            s_adc_state=(int)s_fault;
        }
    }
    if(!s_running && (uint32_t)(now-s_last_at)>=POWER_ADC_PERIOD_MS)
        power_adc_start(now);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc!=0 && hadc->Instance==ADC1)
        s_complete=1u;
}
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc!=0 && hadc->Instance==ADC1)
        s_dma_error=1u;
}

void PowerADC_AddStatisticsItems(ui_page_t *page)
{
    static ui_item_t *const items[4]={
        &s_pa0_item,&s_pa5_item,&s_mv_item,&s_status_item
    };
    static int *const values[4]={
        &s_pa0_raw,&s_pa5_raw,&s_usb_mv,&s_adc_state
    };
    static const char *const names[4]={
        " PA0 I raw"," PA5 V raw"," Vout~ mV"," ADC Err"
    };
    uint8_t i;
    if(!page)return;
    for(i=0u;i<4u;i++)
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
