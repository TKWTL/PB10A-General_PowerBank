/* PB10A minimal OLED bring-up: coroOS drives asynchronous SPI DMA and MiaoUI. */
#include "oled_app.h"
#include "coroOS.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "ssd1306_display.h"
#include "pm_api.h"

static ui_t s_ui;
static coro_scheduler_t s_scheduler;
static coro_pt_t s_states[3];

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
            ui_loop(&s_ui);
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
static const coro_thread_fn_t s_threads[] = {
    OledServiceThread, MiaoUiThread, PowerManagerThread
};

void OLED_AppInit(void)
{
    if(!diapInit()) return;
    MiaoUi_Setup(&s_ui);
    pm_api_init();
    CoroOS_Init(&s_scheduler, s_threads, s_states,
                (uint8_t)(sizeof(s_threads) / sizeof(s_threads[0])));
}

void OLED_AppRunOnce(void)
{
    CoroOS_RunOnce(&s_scheduler);
}
