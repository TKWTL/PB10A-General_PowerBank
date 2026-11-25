/* MMC5603: asynchronous single-shot magnetic field service. */
#include "mmc5603_algo.h"
#include "mmc5603.h"
#include "sensor_common.h"
#include "pt.h"
#define MMC_SAMPLE_MS 250u
#define MMC_RETRY_MS 2000u
#define MMC_CONVERT_MS 10u
#define MMC_CONVERT_TIMEOUT_MS 30u

static uint8_t s_stage,s_waiting,s_paused,s_buffer[9];
static struct pt s_transfer_pt;
static volatile uint8_t s_flag;
static uint32_t s_due_at,s_conversion_at;
static int s_mg[3];
static ui_item_t s_items[3];
static ui_data_t s_data[3];
static ui_element_t s_elements[3];

static uint8_t mmc_transfer(uint8_t read, uint8_t reg, uint8_t count) {
    s_waiting = (uint8_t)PT_SCHEDULE(MMC5603_TransferPT(&s_transfer_pt, read, reg, s_buffer, count, &s_flag));
    return s_waiting ? 0u : s_flag;
}

static void mmc_offline(void) {
    s_stage=0u;s_due_at=Millis()+MMC_RETRY_MS;
}

void MMC5603Algo_Poll(void) {
    uint8_t result;
    if(s_paused && !s_waiting)return;
    if(!s_waiting && (int32_t)(Millis()-s_due_at)<0 && (s_stage==0u || s_stage==1u))return;
    switch(s_stage) {
    case 0u:
        result=mmc_transfer(1u,MMC5603_REG_PRODUCT_ID,1u);
        if(!result)return;
        if(result!=1u || s_buffer[0]!=MMC5603_PRODUCT_ID){mmc_offline();return;}
        s_due_at=Millis();
        s_stage=1u;break;
    case 1u:
        if(!s_waiting)s_buffer[0]=MMC5603_CTRL0_TM_M;
        result=mmc_transfer(0u,MMC5603_REG_CTRL0,1u);
        if(!result)return;
        if(result!=1u){mmc_offline();return;}
        s_conversion_at=Millis();
        s_stage=2u;break;
    case 2u:
        if((uint32_t)(Millis()-s_conversion_at)<MMC_CONVERT_MS)return;
        s_stage=3u;break;
    case 3u:
        result=mmc_transfer(1u,MMC5603_REG_STATUS1,1u);
        if(!result)return;
        if(result!=1u){mmc_offline();return;}
        if (!(s_buffer[0] & MMC5603_STATUS_M_DONE)) {
            if ((uint32_t)(Millis() - s_conversion_at) >= MMC_CONVERT_TIMEOUT_MS) mmc_offline();
            return;
        }
        s_stage=4u;break;
    case 4u:
        result=mmc_transfer(1u,MMC5603_REG_XOUT0,9u);
        if(!result)return;
        if(result!=1u){mmc_offline();return;}
        MMC5603_ParseAxes(s_buffer,&s_mg[0],&s_mg[1],&s_mg[2]);
        s_due_at=Millis()+MMC_SAMPLE_MS;
        s_stage=1u;break;
    default:mmc_offline();break;
    }
}

static pm_hook_result_t mmc_prepare(void *ctx) {
    (void)ctx;
    s_paused=1u;
    return s_waiting?PM_HOOK_PENDING:PM_HOOK_DONE;
}
static pm_hook_result_t mmc_resume(void *ctx) {
    (void)ctx;
    s_paused=0u;
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
    "mmc5603",mmc_prepare,0,mmc_resume,0,81u,0,0
};

void MMC5603Algo_Init(void) {
    s_stage=0u;s_waiting=0u;s_paused=0u;
    s_due_at=Millis();
    PT_INIT(&s_transfer_pt);
    (void)pm_device_register(&s_node);
}

void MMC5603Algo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[3] = {" Mag X mG", " Mag Y mG", " Mag Z mG"};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<3u;i++) {
        s_data[i].name=names[i];s_data[i].ptr=&s_mg[i];
        s_data[i].dataType=UI_DATA_INT;s_data[i].actionType=UI_DATA_ACTION_RO;
        s_elements[i].data=&s_data[i];
        Create_element(&s_items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_items[i],page,0,0);
    }
}
