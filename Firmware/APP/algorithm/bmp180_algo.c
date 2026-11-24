/* BMP180: nonblocking calibration and alternating temperature/pressure samples. */
#include "bmp180_algo.h"
#include "bmp180.h"
#include "sensor_common.h"
#define BMP_SAMPLE_MS 500u
#define BMP_RETRY_MS 2000u

static uint8_t s_stage,s_waiting,s_paused,s_buffer[22];
static volatile uint8_t s_flag;
static uint32_t s_due_at,s_conversion_at;
static int s_temp10,s_pressure_hpa10;
static ui_item_t s_items[2];
static ui_data_t s_data[2];
static ui_element_t s_elements[2];

static uint8_t bmp_transfer(uint8_t read,uint8_t reg,uint8_t count) {
    if(!s_waiting) {
        if(!(read?BMP180_AsyncRead(reg,s_buffer,count,(uint8_t *)&s_flag):
            BMP180_AsyncWrite(reg,s_buffer,count,(uint8_t *)&s_flag)))return 0u;
        s_waiting=1u;
        return 0u;
    }
    if(s_flag==I2C_ASYNC_PENDING)return 0u;
    s_waiting=0u;
    return s_flag==I2C_ASYNC_DONE?1u:2u;
}

static void bmp_offline(void) {
    s_stage=0u;
    s_due_at=Millis()+BMP_RETRY_MS;
}

__attribute__((noinline)) void BMP180Algo_Poll(void) {
    uint8_t result;
    if(s_paused && !s_waiting)return;
    if(!s_waiting && (s_stage==0u || s_stage==2u) &&
        (int32_t)(Millis()-s_due_at)<0)return;
    switch(s_stage) {
    case 0u:
        result=bmp_transfer(1u,BMP180_ID,1u);
        if(!result)return;
        if(result!=1u || s_buffer[0]!=0x55u){bmp_offline();return;}
        s_stage=1u;break;
    case 1u:
        result=bmp_transfer(1u,BMP180_CALVALUE_BASE,22u);
        if(!result)return;
        if(result!=1u || !BMP180_LoadCalibration(s_buffer)){bmp_offline();return;}
        s_due_at=Millis();s_stage=2u;break;
    case 2u:
        if(!s_waiting)s_buffer[0]=BMP180_CMD_TEMP;
        result=bmp_transfer(0u,BMP180_CONTROL,1u);
        if(!result)return;
        if(result!=1u){bmp_offline();return;}
        s_conversion_at=Millis();s_stage=3u;break;
    case 3u:
        if((uint32_t)(Millis()-s_conversion_at)<5u)return;
        s_stage=4u;break;
    case 4u:
        result=bmp_transfer(1u,BMP180_ADCVALUE_BASE,2u);
        if(!result)return;
        if(result!=1u || !BMP180_ApplyTemperature(s_buffer,&s_temp10)){bmp_offline();return;}
        s_stage=5u;break;
    case 5u:
        if(!s_waiting)s_buffer[0]=BMP180_CMD_PRESS_OSS3;
        result=bmp_transfer(0u,BMP180_CONTROL,1u);
        if(!result)return;
        if(result!=1u){bmp_offline();return;}
        s_conversion_at=Millis();s_stage=6u;break;
    case 6u:
        if((uint32_t)(Millis()-s_conversion_at)<26u)return;
        s_stage=7u;break;
    case 7u:
        result=bmp_transfer(1u,BMP180_ADCVALUE_BASE,3u);
        if(!result)return;
        if(result!=1u || !BMP180_ApplyPressure(s_buffer,&s_pressure_hpa10)){bmp_offline();return;}
        s_due_at=Millis()+BMP_SAMPLE_MS;
        s_stage=2u;break;
    default:bmp_offline();break;
    }
}

static pm_hook_result_t bmp_prepare(void *ctx) {
    (void)ctx;
    s_paused=1u;
    return s_waiting?PM_HOOK_PENDING:PM_HOOK_DONE;
}
static pm_hook_result_t bmp_resume(void *ctx) {
    (void)ctx;
    s_paused=0u;
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
    "bmp180",bmp_prepare,0,bmp_resume,0,82u,0,0
};
void BMP180Algo_Init(void) {
    s_stage=0u;s_waiting=0u;s_paused=0u;s_due_at=Millis();
    (void)pm_device_register(&s_node);
}
void BMP180Algo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[2]={" Temp C"," Press hPa"};
    int *values[2]={&s_temp10,&s_pressure_hpa10};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<2u;i++) {
        s_data[i].name=names[i];s_data[i].ptr=values[i];
        s_data[i].dataType=UI_DATA_FLOAT;s_data[i].actionType=UI_DATA_ACTION_RO;
        s_data[i].decimals=1u;
        s_elements[i].data=&s_data[i];
        Create_element(&s_items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_items[i],page,0,0);
    }
}
