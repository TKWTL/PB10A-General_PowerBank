/* RDA5807 low-power policy. Force ENABLE=0 after I2C initialization.
 * MCO=32.768 kHz is provided only for the power-down transaction and while
 * an explicitly started radio session is active. Before UI_OFF/STOP1 power
 * down even a receiver left in RADIO_ERROR. Wake leaves MCO disabled. */
#include "radio_algo.h"
#include "radio_app.h"
#include "rda5807m.h"
#include "pm_device.h"
static uint8_t s_boot_sleep_ack,s_io_error;
static pm_hook_result_t radio_prepare(void *ctx) {
    (void)ctx;
    Radio_AppStop(); /* Also retries power-down after RADIO_ERROR. */
    return Radio_AppStopPending()?PM_HOOK_PENDING:PM_HOOK_DONE;
}

static pm_hook_result_t radio_resume(void *ctx) {
    (void)ctx;
    return PM_HOOK_DONE; /* MCO remains off until Radio_AppStart(). */
}

static const pm_device_node_t s_node={
    "rda5807",radio_prepare,0,radio_resume,0,110u,0,0
};
void RadioAlgo_Init(void) {
    s_boot_sleep_ack=Radio_AppInitLowPower();
    (void)pm_device_register(&s_node);
}

uint8_t RadioAlgo_BootSleepAcknowledged(void)
{return (uint8_t)(s_boot_sleep_ack && !RadioAlgo_IOBusy() && !s_io_error);}

/*********************** Serialized asynchronous RDA5807 IO ******************/
#include "i2c.h"
#define RADIO_QUEUE_SIZE 12u
typedef struct {uint8_t reg;uint16_t value;} radio_cmd_t;
static radio_cmd_t s_queue[RADIO_QUEUE_SIZE];
static uint8_t s_head,s_count,s_active,s_kind,s_query_stage,s_status_ready,s_discard_status;
static uint8_t s_status_error,s_data[2];
static uint16_t s_status_word,s_signal_word;
static volatile uint8_t s_flag;

uint8_t RadioAlgo_QueueWrite(uint8_t reg,uint16_t value) {
    uint8_t tail;
    if(s_count>=RADIO_QUEUE_SIZE)return I2C_ERROR;
    tail=(uint8_t)((s_head+s_count)%RADIO_QUEUE_SIZE);
    s_queue[tail].reg=reg;
    s_queue[tail].value=value;
    ++s_count;
    s_status_ready=0u;
    if(s_query_stage)s_discard_status=1u;
    return I2C_OK;
}

uint8_t RadioAlgo_TakeStatus(uint16_t *status,uint16_t *signal) {
    if(s_status_error) {s_status_error=0u;return I2C_ERROR;}
    if(s_status_ready) {
        if(status)*status=s_status_word;
        if(signal)*signal=s_signal_word;
        s_status_ready=0u;
        return I2C_OK;
    }
    if(!s_query_stage)s_query_stage=1u;
    return RADIO_IO_PENDING;
}

void RadioAlgo_ResetStatus(void) {
    s_io_error=0u;s_status_error=0u;s_status_ready=0u;
    s_discard_status=0u;
}

uint8_t RadioAlgo_TakeIOError(void) {
    uint8_t error=s_io_error;
    s_io_error=0u;
    return error;
}

uint8_t RadioAlgo_IOBusy(void) {
    return (uint8_t)(s_active || s_count || s_query_stage);
}

void RadioAlgo_Service(void) {
    uint8_t reg;
    if(s_active) {
        if(s_flag==I2C_ASYNC_PENDING)return;
        s_active=0u;
        if(s_flag!=I2C_ASYNC_DONE) {
            s_io_error=1u;
            s_status_error=1u;
            s_count=0u;
            s_query_stage=0u;
            return;
        }
        if(s_kind==0u) {
            s_head=(uint8_t)((s_head+1u)%RADIO_QUEUE_SIZE);
            --s_count;
        }
        else if(s_kind==1u) {
            s_status_word=(uint16_t)(((uint16_t)s_data[0]<<8)|s_data[1]);
            s_query_stage=2u;
        }
        else {
            s_signal_word=(uint16_t)(((uint16_t)s_data[0]<<8)|s_data[1]);
            s_query_stage=0u;
            s_status_ready=(uint8_t)!s_discard_status;
            s_discard_status=0u;
        }
    }
    if(s_count) {
        s_data[0]=(uint8_t)(s_queue[s_head].value>>8);
        s_data[1]=(uint8_t)s_queue[s_head].value;
        if(ASYNC_I2C_Transmit(RDA5807_Wr_REG_Addr,s_queue[s_head].reg,
            s_data,2u,0u,(uint8_t *)&s_flag)) {
            s_kind=0u;s_active=1u;
        }
        return;
    }
    if(!s_query_stage)return;
    reg=(s_query_stage==1u)?RDA5807_REG0A:RDA5807_REG0B;
    if(ASYNC_I2C_Receive(RDA5807_Wr_REG_Addr,reg,
        s_data,2u,0u,(uint8_t *)&s_flag)) {
        s_kind=s_query_stage;s_active=1u;
    }
}
