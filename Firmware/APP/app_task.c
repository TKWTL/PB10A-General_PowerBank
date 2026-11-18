/* PB10A task table: MiaoUI, OLED DMA, PM and shared services. */
#include "app_task.h"
#include "coroOS.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "ssd1306_display.h"
#include "pm_api.h"
#include "millis.h"
#include "time_service.h"
#include "time_algo.h"
#include "beep_algo.h"
#include "system_algo.h"
#include "sc7a20.h"
#include "hw_test.h"
#include "power_adc_algo.h"

static ui_t s_ui;
static uint32_t s_last_time_update_ms;
static coro_scheduler_t s_scheduler;
static coro_pt_t s_states[4];

THRD_DECLARE(OledServiceThread)
{
    THRD_BEGIN;
    for(;;)
    {
        SSD1306_Display_Service();
        THRD_DELAY(1u);
    }
    THRD_END;
}

THRD_DECLARE(MiaoUiThread)
{
    THRD_BEGIN;
    for(;;)
    {
        if(SSD1306_Display_IsReady() && !SSD1306_Display_FrameBusy() && !pm_api_ui_should_block())
        {
            /* Live RTC display/edit fields, but never overwrite a currently
             * edited field with an asynchronous RTC read. */
            if((uint32_t)(Millis()-s_last_time_update_ms)>=1000u &&
               s_ui.menuState!=UI_ITEM_RUNING &&
               s_ui.menuState!=UI_ITEM_DRAWING)
            {
                s_last_time_update_ms=Millis();
                RTC_UpdateTime();
            }
            ui_loop(&s_ui);
        }
        THRD_DELAY(20u);
    }
    THRD_END;
}

THRD_DECLARE(PowerManagerThread)
{
    THRD_BEGIN;
    for(;;){pm_api_poll();THRD_DELAY(50u);}
    THRD_END;
}
THRD_DECLARE(SystemServiceThread)
{
    THRD_BEGIN;
    for(;;)
    {
        beep_poll(Millis());
        System_Poll();
        PowerADC_Poll();
        THRD_DELAY(1u);
    }
    THRD_END;
}
static const coro_thread_fn_t s_threads[] = {
    OledServiceThread, MiaoUiThread, PowerManagerThread, SystemServiceThread
};

void APP_TaskInit(void)
{
    if(!diapInit()) return;
    beep_init(); /* one-shot hardware/queue init */
    Time_Init(); /* one-shot uptime baseline */
    /* PB02B SC7A20 initialization: WHO_AM_I + 25Hz/2g/BDU/HR.
     * PB10A INT is on PB5, independent from SW6306; no IRQ sharing. */
    SC7A20_Init();
    PowerADC_Init(); /* PA6 stays on; 200ms ADC scans run in services. */
    RTC_UpdateTime();
    MiaoUi_Setup(&s_ui);
    pm_api_init();
    CoroOS_Init(&s_scheduler, s_threads, s_states,
                (uint8_t)(sizeof(s_threads) / sizeof(s_threads[0])));
#if PB10A_HW_TEST_AUTORUN
    HW_TestInit();
#endif
}

void APP_TaskRunOnce(void)
{
#if PB10A_HW_TEST_AUTORUN
    if(HW_TestActive())
    {
        HW_TestPoll(&s_ui);
        return; /* Temporarily show only original FM seek screen at boot. */
    }
#endif
    CoroOS_RunOnce(&s_scheduler);
}
