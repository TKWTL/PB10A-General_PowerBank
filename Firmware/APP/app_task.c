/* PB10A task table: MiaoUI, OLED DMA, PM and shared services. */
#include "app_task.h"
#include "coroOS.h"
#include "core/ui.h"
#include "indev/indevDriver.h"
#include "display/dispDriver.h"
#include "ssd1306_display.h"
#include "pm_api.h"
#include "millis.h"
#include "i2c.h"
#include "time_service.h"
#include "time_algo.h"
#include "beep_algo.h"
#include "system_algo.h"
#include "sc7a20_algo.h"
#include "mmc5603_algo.h"
#include "bmp180_algo.h"
#include "power_algo.h"
#include "radio_algo.h"
#include "radio_app.h"

static ui_t s_ui;
static coro_scheduler_t s_scheduler;
static coro_pt_t s_states[7];

/*************************** 协程服务线程 ***************************/
THRD_DECLARE(OledServiceThread) {
    THRD_BEGIN;
    for(;;) {
        SSD1306_Display_Service();
        THRD_DELAY(1u);
    }
    THRD_END;
}

THRD_DECLARE(MiaoUiThread) {
    THRD_BEGIN;
    for(;;) {
        if(SSD1306_Display_IsReady() && !SSD1306_Display_FrameBusy() && !pm_api_ui_should_block()) {
            MiaoUi_ProcessNavigate(&s_ui);
            Disp_ResumeRotation(&s_ui);
            ui_loop(&s_ui);
        }
        THRD_DELAY(20u);
    }
    THRD_END;
}

THRD_DECLARE(PowerManagerThread) {
    THRD_BEGIN;
    for(;;){pm_api_poll();THRD_DELAY(50u);}
    THRD_END;
}

static uint32_t s_last_key_tick,s_last_radio_poll;
THRD_DECLARE(SystemServiceThread) {
    THRD_BEGIN;
    for(;;) {
        const uint32_t now=Millis();
        if((uint32_t)(now-s_last_key_tick)>=10u) {
            s_last_key_tick=now;
            indevService_10ms();
        }
        /* Keep Radio Power Mode=ON playing and polling after leaving its
         * WORD page, without a blocking radio task. */
        RadioAlgo_Service();
        if((uint32_t)(now-s_last_radio_poll)>=100u) {
            s_last_radio_poll=now;
            if(Radio_AppStopPending() ||
                (Radio_AppIsActive() && !pm_api_ui_should_block()))
                Radio_AppPoll();
        }
        MiaoUi_ExternalEventService();
        beep_poll(now);
        THRD_DELAY(1u);
    }
    THRD_END;
}
/* ADC/DMA, RTC and auxiliary USB-A share a 500 ms service cadence.
 * The ADC settles by IRQ while the other coroOS threads continue running.
 * Refresh RTC each cycle; the Time editor keeps its own write API. */
THRD_DECLARE(ADC_RTC_AuxAThread) {
    THRD_BEGIN;
    PowerAlgo_AdcInit();
    RTC_UpdateTime();
    for(;;) {
        if(!PowerAlgo_AdcIsSuspended() && PowerAlgo_AdcBeginScan()) {
            THRD_UNTIL(PowerAlgo_AdcScanSettled());
        }
        if(!RTC_IsEditing(&s_ui))RTC_UpdateTime();
        PowerAlgo_AuxA_Service();
        System_Poll();
        THRD_DELAY(500u);
    }
    THRD_END;
}
/* The three sensor actors share a lightweight polling thread; each actor
 * owns a persistent I2C buffer, IRQ completion flag and its own timing. */
THRD_DECLARE(SensorServiceThread) {
    THRD_BEGIN;
    for(;;) {
        SC7A20Algo_Poll();
        MMC5603Algo_Poll();
        BMP180Algo_Poll();
        THRD_DELAY(5u);
    }
    THRD_END;
}
/*************************** 线程表与入口 ***************************/
static const coro_thread_fn_t s_threads[] = {
    OledServiceThread, MiaoUiThread, PowerManagerThread,
    SystemServiceThread, ADC_RTC_AuxAThread, PowerAlgo_SW6306Thread,
    SensorServiceThread
};

void APP_TaskInit(void) {
    if(!diapInit()) return;
    beep_init(); /* one-shot hardware/queue init */
    indevInit();
    s_last_key_tick=Millis();
    s_last_radio_poll=Millis();
    Time_Init(); /* keep the underlying RTC uptime API, without a UI item */
    /* Sensors initialize through their own nonblocking service actors. */
    PowerAlgo_Init();  /* SW6306 monitor, sleep gating, auxiliary A ADC. */
    SC7A20Algo_Init();
    MMC5603Algo_Init();
    BMP180Algo_Init();
    RadioAlgo_Init();  /* RDA5807 powers down before UI_OFF. */
    MiaoUi_Setup(&s_ui);
    pm_api_init();
    CoroOS_Init(&s_scheduler, s_threads, s_states,
        (uint8_t)(sizeof(s_threads) / sizeof(s_threads[0])));
}

void APP_TaskRunOnce(void) {
    /* IRQ continues I2C, this watchdog only bounds a stalled request. */
    I2C_AsyncService();
    CoroOS_RunOnce(&s_scheduler);
}
