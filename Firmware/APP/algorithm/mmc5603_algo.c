/* MMC5603: asynchronous single-shot magnetic field service. */
#include "mmc5603_algo.h"
#include "mmc5603.h"
#include "sensor_common.h"
#include "pt.h"
#define MMC_SAMPLE_MS 250u
#define MMC_TOOL_SAMPLE_MS 40u
#define MMC_RETRY_MS 2000u
#define MMC_RESET_MS 20u
#define MMC_CONVERT_MS 10u
#define MMC_CONVERT_TIMEOUT_MS 30u

enum {
    MMC_RESET,
    MMC_RESET_WAIT,
    MMC_CHECK_ID,
    MMC_CONFIG_CTRL1,
    MMC_CONFIG_CTRL2,
    MMC_SET,
    MMC_TRIGGER,
    MMC_CONVERT_WAIT,
    MMC_STATUS,
    MMC_READ_AXES
};

static uint8_t s_stage,s_waiting,s_paused,s_buffer[9];
static struct pt s_transfer_pt;
static volatile uint8_t s_flag;
static uint32_t s_due_at,s_conversion_at;
static mmc5603_sample_t s_sample;
static uint8_t s_valid, s_tool_mode;
static ui_item_t s_items[3];
static ui_data_t s_data[3];
static ui_element_t s_elements[3];

static uint8_t mmc_transfer(uint8_t read, uint8_t reg, uint8_t count) {
    s_waiting = (uint8_t)PT_SCHEDULE(MMC5603_TransferPT(&s_transfer_pt, read, reg, s_buffer, count, &s_flag));
    return s_waiting ? 0u : s_flag;
}

static uint8_t mmc_write(uint8_t reg, uint8_t value) {
    if (!s_waiting)
        s_buffer[0] = value;
    return mmc_transfer(0u, reg, 1u);
}

static void mmc_offline(void) {
    s_valid = 0u;
    s_stage = MMC_RESET;
    s_due_at = Millis() + MMC_RETRY_MS;
}

uint8_t MMC5603Algo_GetSample(mmc5603_sample_t *out) {
    if (!out || !s_valid || s_paused || (uint32_t)(Millis() - s_sample.at_ms) > 800u)
        return 0u;
    *out = s_sample;
    return 1u;
}

void MMC5603Algo_SetToolMode(uint8_t enable) {
    s_tool_mode = enable != 0u;
    if (s_stage == MMC_TRIGGER && !s_waiting)
        s_due_at = Millis() + (s_tool_mode ? 0u : MMC_SAMPLE_MS);
}

void MMC5603Algo_Poll(void) {
    uint8_t result;
    if (s_paused && !s_waiting)
        return;
    if (!s_waiting && (s_stage == MMC_RESET || s_stage == MMC_RESET_WAIT || s_stage == MMC_TRIGGER) &&
        (int32_t)(Millis() - s_due_at) < 0)
        return;

    switch (s_stage) {
    case MMC_RESET:
        result = mmc_write(MMC5603_REG_CTRL1, MMC5603_CTRL1_SW_RESET);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        /* Rev. B startup: reset clears registers and reloads OTP. */
        s_due_at = Millis() + MMC_RESET_MS;
        s_stage = MMC_RESET_WAIT;
        break;
    case MMC_RESET_WAIT:
        s_stage = MMC_CHECK_ID;
        break;
    case MMC_CHECK_ID:
        result = mmc_transfer(1u, MMC5603_REG_PRODUCT_ID, 1u);
        if (!result)
            return;
        if (result != 1u || s_buffer[0] != MMC5603_PRODUCT_ID) {
            mmc_offline();
            return;
        }
        s_stage = MMC_CONFIG_CTRL1;
        break;
    case MMC_CONFIG_CTRL1:
    case MMC_CONFIG_CTRL2:
        /* BW=00, XYZ enabled; continuous mode and periodic SET disabled. */
        result = mmc_write(s_stage == MMC_CONFIG_CTRL1 ? MMC5603_REG_CTRL1 : MMC5603_REG_CTRL2, 0u);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        ++s_stage;
        break;
    case MMC_SET:
        result = mmc_write(MMC5603_REG_CTRL0, MMC5603_CTRL0_SET);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        s_due_at = Millis();
        s_stage = MMC_TRIGGER;
        break;
    case MMC_TRIGGER:
        result = mmc_write(MMC5603_REG_CTRL0, MMC5603_CTRL0_TM_M | MMC5603_CTRL0_AUTO_SR);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        s_conversion_at = Millis();
        s_stage = MMC_CONVERT_WAIT;
        break;
    case MMC_CONVERT_WAIT:
        if ((uint32_t)(Millis() - s_conversion_at) < MMC_CONVERT_MS)
            return;
        s_stage = MMC_STATUS;
        break;
    case MMC_STATUS:
        result = mmc_transfer(1u, MMC5603_REG_STATUS1, 1u);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        if (!(s_buffer[0] & MMC5603_STATUS_M_DONE)) {
            if ((uint32_t)(Millis() - s_conversion_at) >= MMC_CONVERT_TIMEOUT_MS)
                mmc_offline();
            return;
        }
        s_stage = MMC_READ_AXES;
        break;
    case MMC_READ_AXES:
        result = mmc_transfer(1u, MMC5603_REG_XOUT0, 9u);
        if (!result)
            return;
        if (result != 1u) {
            mmc_offline();
            return;
        }
        MMC5603_ParseAxes(s_buffer, &s_sample.mg[0], &s_sample.mg[1], &s_sample.mg[2]);
        s_sample.at_ms = Millis();
        ++s_sample.seq;
        s_valid = 1u;
        /* Tool cadence is measured from the trigger, including conversion. */
        s_due_at = s_tool_mode ? s_conversion_at + MMC_TOOL_SAMPLE_MS : s_sample.at_ms + MMC_SAMPLE_MS;
        s_stage = MMC_TRIGGER;
        break;
    default:
        mmc_offline();
        break;
    }
}

static pm_hook_result_t mmc_prepare(void *ctx) {
    (void)ctx;
    s_paused=1u;
    s_valid = 0u;
    return s_waiting?PM_HOOK_PENDING:PM_HOOK_DONE;
}
static pm_hook_result_t mmc_resume(void *ctx) {
    (void)ctx;
    s_paused=0u;
    s_valid = 0u;
    return PM_HOOK_DONE;
}
static const pm_device_node_t s_node={
    "mmc5603",mmc_prepare,0,mmc_resume,0,81u,0,0
};

void MMC5603Algo_Init(void) {
    s_stage = MMC_RESET;
    s_waiting = s_paused = 0u;
    s_due_at=Millis();
    s_valid = s_tool_mode = 0u;
    s_sample.seq = 0u;
    PT_INIT(&s_transfer_pt);
    (void)pm_device_register(&s_node);
}

void MMC5603Algo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[3] = {" Mag X mG", " Mag Y mG", " Mag Z mG"};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<3u;i++) {
        s_data[i].name = names[i];
        s_data[i].ptr = &s_sample.mg[i];
        s_data[i].dataType=UI_DATA_INT;s_data[i].actionType=UI_DATA_ACTION_RO;
        s_elements[i].data=&s_data[i];
        Create_element(&s_items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_items[i],page,0,0);
    }
}
