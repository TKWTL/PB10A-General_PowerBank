/* SC7A20: IRQ-driven I2C initialization, XYZ sampling and STOP1 policy. */
#include "sc7a20_algo.h"
#include "sc7a20.h"
#include "i2c.h"
#include "millis.h"
#include "pm_device.h"
#include "core/ui.h"

#define SC_SAMPLE_MS 200u
#define SC_RETRY_MS 2000u
#define SC_CTRL1_RUN (SC7A20_DEFAULT_ODR | SC7A20_DEFAULT_AXIS_EN)
#define SC_CTRL4_RUN ((SC7A20_DEFAULT_BDU ? SC7A20_CTRL4_BDU : 0u) | SC7A20_DEFAULT_FULLSCALE | (SC7A20_DEFAULT_HIGHRES ? SC7A20_CTRL4_HR : 0u))

static uint8_t s_stage,s_waiting,s_paused,s_suspended,s_pm_waiting,s_pm_target;
static uint8_t s_buffer[6];
static volatile uint8_t s_flag,s_pm_flag;
static uint32_t s_last_sample,s_retry_at;
static int s_axes[3];
static ui_item_t s_items[3];
static ui_data_t s_data[3];
static ui_element_t s_elements[3];

static uint8_t sc_transfer(uint8_t read,uint8_t reg,uint8_t count) {
    if(!s_waiting) {
        if(!(read?ASYNC_I2C_Receive(SC7A20_I2C_ADDR,reg,s_buffer,count,0u,(uint8_t *)&s_flag):
            ASYNC_I2C_Transmit(SC7A20_I2C_ADDR,reg,s_buffer,count,0u,(uint8_t *)&s_flag)))return 0u;
        s_waiting=1u;
        return 0u;
    }
    if(s_flag==I2C_ASYNC_PENDING)return 0u;
    s_waiting=0u;
    return s_flag==I2C_ASYNC_DONE?1u:2u;
}

static void sc_offline(void) {
    s_stage=0u;
    s_retry_at=Millis()+SC_RETRY_MS;
    SC7A20_AsyncSetReady(0u);
}

void SC7A20Algo_Poll(void) {
    uint8_t result;
    if(s_suspended || (s_paused && !s_waiting))return;
    if(!s_waiting && s_stage==0u && (int32_t)(Millis()-s_retry_at)<0)return;
    switch(s_stage) {
    case 0u:
        result=sc_transfer(1u,SC7A20_STRG_WHO_AM_I,1u);
        if(!result)return;
        if(result!=1u || s_buffer[0]!=SC7A20_WHO_AM_I_VALUE){sc_offline();return;}
        s_stage=1u;break;
    case 1u:
        result=sc_transfer(1u,SC7A20_CTRG_CTRL6,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_buffer[0]|=SC7A20_CTRL6_H_LACTIVE;
        s_stage=2u;break;
    case 2u:
        if(!s_waiting)s_buffer[0]|=SC7A20_CTRL6_H_LACTIVE;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL6,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=3u;break;
    case 3u:
        if(!s_waiting)s_buffer[0]=SC_CTRL4_RUN;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL4,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=4u;break;
    case 4u:
        if(!s_waiting)s_buffer[0]=0xC0u; /* existing LIS2DH-compatible temperature setup */
        result=sc_transfer(0u,SC7A20_CTRG_TEMP_CFG,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=5u;break;
    case 5u:
        if(!s_waiting)s_buffer[0]=SC_CTRL1_RUN;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL1,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        SC7A20_AsyncSetReady(1u);
        s_last_sample=Millis()-SC_SAMPLE_MS;
        s_stage=6u;break;
    case 6u:
        if(!s_waiting && (uint32_t)(Millis()-s_last_sample)<SC_SAMPLE_MS)return;
        result=sc_transfer(1u,SC7A20_STRG_OUT_AUTO,6u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        SC7A20_AsyncSetAxes(s_buffer);
        /* HR at 2g: one right-aligned code is one mg. */
        {int16_t x,y,z;
            SC7A20_ReadRawCode(&x,&y,&z);
            s_axes[0]=(int)x;s_axes[1]=(int)y;s_axes[2]=(int)z;
        }
        s_last_sample=Millis();break;
    default:sc_offline();break;
    }
}

static pm_hook_result_t sc_prepare(void *ctx) {
    (void)ctx;
    s_paused=1u;
    return s_waiting?PM_HOOK_PENDING:PM_HOOK_DONE;
}

static pm_hook_result_t sc_pm_update(uint8_t resume) {
    if(s_pm_waiting) {
        if(s_pm_flag==I2C_ASYNC_PENDING)return PM_HOOK_PENDING;
        s_pm_waiting=0u;
        if(s_pm_flag!=I2C_ASYNC_DONE)return PM_HOOK_ERROR;
        s_suspended=s_pm_target;
    }
    if(resume) {
        if(!s_suspended){s_paused=0u;return PM_HOOK_DONE;}
        s_buffer[0]=SC_CTRL1_RUN;
    }
    else {
        if(s_stage!=6u || s_suspended)return PM_HOOK_DONE;
        s_buffer[0]=(uint8_t)(SC_CTRL1_RUN & ~SC7A20_CTRL1_ODR_MSK);
    }
    if(!ASYNC_I2C_Transmit(SC7A20_I2C_ADDR,SC7A20_CTRG_CTRL1,
        s_buffer,1u,0u,(uint8_t *)&s_pm_flag))return PM_HOOK_PENDING;
    s_pm_target=resume?0u:1u;
    s_pm_waiting=1u;
    return PM_HOOK_PENDING;
}

static pm_hook_result_t sc_suspend(void *ctx) {
    (void)ctx;
    return sc_pm_update(0u);
}

static pm_hook_result_t sc_resume(void *ctx) {
    pm_hook_result_t result;
    (void)ctx;
    result=sc_pm_update(1u);
    if(result==PM_HOOK_ERROR)s_paused=0u;
    return result;
}

static const pm_device_node_t s_node={
    "sc7a20",sc_prepare,sc_suspend,sc_resume,0,80u,0,0
};

void SC7A20Algo_Init(void) {
    s_stage=0u;s_waiting=0u;s_paused=0u;s_suspended=0u;
    s_pm_waiting=0u;s_retry_at=Millis();
    SC7A20_AsyncSetReady(0u);
    (void)pm_device_register(&s_node);
}

void SC7A20Algo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[3]={" Acc X mg"," Acc Y mg"," Acc Z mg"};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<3u;i++) {
        s_data[i].name=names[i];s_data[i].ptr=&s_axes[i];
        s_data[i].dataType=UI_DATA_INT;s_data[i].actionType=UI_DATA_ACTION_RO;
        s_elements[i].data=&s_data[i];
        Create_element(&s_items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_items[i],page,0,0);
    }
}
