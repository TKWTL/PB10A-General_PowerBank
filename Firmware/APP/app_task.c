/* PB10A task table: MiaoUI, OLED DMA, PM and shared services. */
#include "app_task.h"
#include "coroOS.h"
#include "miaoui.h"
#include "ssd1306_display.h"
#include "pm_api.h"
#include "millis.h"
#include "i2c.h"
#include "time_service.h"
#include "beep_service.h"
#include "algorithms.h"
#include "radio_app.h"
static ui_t s_ui;
static coro_scheduler_t s_scheduler;
static coro_pt_t s_states[10];

/*************************** 协程服务线程 ***************************/
THRD_DECLARE(OledServiceThread) {
    THRD_BEGIN;
    for(;;) {
        SSD1306_Display_Service();
        THRD_DELAY(1u);
    }
    THRD_END;
}

static uint32_t s_ui_next_deadline, s_ui_next_interval;
static uint8_t s_ui_woke_early;
THRD_DECLARE(MiaoUiThread) {
    THRD_BEGIN;
    s_ui_next_deadline = Millis();
    for(;;) {
        /* A key or completed navigation arbitration may wake a slow page.
         * During the mosaic fade, key events must not accelerate its steps. */
        THRD_UNTIL((int32_t)(Millis() - s_ui_next_deadline) >= 0 ||
                   (s_ui.fadePhase == 0u && indevHasPending()) ||
                   MiaoUi_NavigationReady());
        s_ui_woke_early = (uint8_t)((int32_t)(Millis() - s_ui_next_deadline) < 0);

        /* The SPI DMA reads the shared framebuffer in place. Waiting here
         * does not count a missed drawing opportunity as a completed frame. */
        THRD_UNTIL(SSD1306_Display_IsReady() &&
                   !SSD1306_Display_FrameBusy() &&
                   !pm_api_ui_should_block());

        MiaoUi_ProcessNavigate(&s_ui);
        /* Leveler and calibration views retain their physical orientation. */
        if (SC7A20Algo_ToolMode() != 0u) Disp_SetRotation(0u);
        else Disp_ResumeRotation(&s_ui);

        s_ui_next_interval = ui_loop(&s_ui);
        /* Advance absolute deadlines without accumulating 1 ms scheduler
         * jitter. Skip missed slots rather than running catch-up frames.
         * Key/navigation wakes begin a fresh interval from this frame. */
        if (s_ui_woke_early ||
            (int32_t)(Millis() - s_ui_next_deadline) >= (int32_t)s_ui_next_interval)
            s_ui_next_deadline = Millis() + s_ui_next_interval;
        else
            s_ui_next_deadline += s_ui_next_interval;
    }
    THRD_END;
}

THRD_DECLARE(PowerManagerThread) {
    THRD_BEGIN;
    for(;;){pm_api_poll();THRD_DELAY(50u);}
    THRD_END;
}

static uint32_t s_last_key_tick;
THRD_DECLARE(SystemServiceThread) {
    THRD_BEGIN;
    for(;;) {
        const uint32_t now=Millis();
        if((uint32_t)(now-s_last_key_tick)>=10u) {
            s_last_key_tick=now;
            indevService_10ms();
        }
        MiaoUi_ExternalEventService();
        beep_poll(now);
        THRD_DELAY(1u);
    }
    THRD_END;
}
/* Collar-Radio-style software mean: 16 independent four-rank ADC DMA
 * scans per 500 ms period, with 25 ms cooperative delays between
 * completed scans (15 gaps = 375 ms nominal, leaving margin before
 * the next 500 ms deadline). No new DMA timeout/retry machinery.
 * The ADC IRQ signals completion; the 16 means are published as one
 * snapshot while RTC/Aux-A retain their existing 500 ms cadence. */
#define ADC_SCAN_GAP_MS 25u
static uint8_t s_adc_scan_index; /* persistent: protothreads are stackless */
static uint32_t s_adc_cycle_at;
THRD_DECLARE(ADC_RTC_AuxAThread) {
    THRD_BEGIN;
    PowerAlgo_AdcInit();
    RTC_UpdateTime();
    for(;;) {
        s_adc_cycle_at=Millis();
        PowerAlgo_AdcBatchBegin();
        for(s_adc_scan_index=0u;
            s_adc_scan_index<POWER_ADC_AVERAGE_SAMPLES;
            ++s_adc_scan_index) {
            if(PowerAlgo_AdcIsSuspended() || !PowerAlgo_AdcBeginScan())
                break;
            THRD_UNTIL(PowerAlgo_AdcScanSettled());
            if(s_adc_scan_index+1u<POWER_ADC_AVERAGE_SAMPLES)
                THRD_DELAY(ADC_SCAN_GAP_MS);
        }
        if(!RTC_IsEditing(&s_ui))RTC_UpdateTime();
        PowerAlgo_AuxA_Service();
        System_Poll();
        /* Preserve the original 500 ms period from burst START,
         * rather than adding 500 ms after the 16 spaced scans. */
        THRD_UNTIL((uint32_t)(Millis()-s_adc_cycle_at)>=500u);
    }
    THRD_END;
}
/* FM owns the hardware command state machine and IRQ-backed BSP. */
THRD_DECLARE(RadioServiceThread) {
    THRD_BEGIN;
    for(;;) {
        RadioAlgo_Poll();
        THRD_DELAY(5u);
    }
    THRD_END;
}

/* Each I2C sensor has a cooperative nonblocking service actor.
 * SC7A20 has a sensor data-ready interrupt capability; MMC5603 and
 * BMP180 are sampled by periodic software polling. These sensor-side
 * capabilities are separate from the interrupt-driven I2C transport,
 * which is shared by all three actors. */
THRD_DECLARE(SC7A20Thread) {
    THRD_BEGIN;
    for(;;) {
        SC7A20Algo_Poll();
        THRD_DELAY(5u);
    }
    THRD_END;
}
THRD_DECLARE(MMC5603Thread) {
    THRD_BEGIN;
    for(;;) {
        MMC5603Algo_Poll();
        THRD_DELAY(5u);
    }
    THRD_END;
}
THRD_DECLARE(BMP180Thread) {
    THRD_BEGIN;
    for(;;) {
        BMP180Algo_Poll();
        THRD_DELAY(5u);
    }
    THRD_END;
}

/********************** coroOS thread registration **********************
 * Layout groups event/UI, power, radio and independent sensor services.
 * Keep the s_states count in sync with this fixed thread list. */
static const coro_thread_fn_t s_threads[] = {
    /* Display and user interaction */
    OledServiceThread,
    MiaoUiThread,
    SystemServiceThread,

    /* Power management and charge/ADC */
    PowerManagerThread,
    ADC_RTC_AuxAThread,
    PowerAlgo_SW6306Thread,

    /* Dedicated asynchronous FM service */
    RadioServiceThread,

    /* Independent sensor poll actors: SC7A20 IRQ-capable;
     * MMC5603/BMP180 polling-only (I2C transfers themselves use IRQ). */
    SC7A20Thread,
    MMC5603Thread,
    BMP180Thread,
};

void APP_TaskInit(void) {
    if(!diapInit()) return;
    beep_init(); /* one-shot hardware/queue init */
    indevInit();
    s_last_key_tick=Millis();
    Time_Init(); /* keep the underlying RTC uptime API, without a UI item */
    /* Sensors initialize through their own nonblocking service actors. */
    PowerAlgo_Init();  /* SW6306 monitor, sleep gating, auxiliary A ADC. */
    SC7A20Algo_Init();
    MMC5603Algo_Init();
    BMP180Algo_Init();
    RadioAlgo_Init(); /* BSP async boot sleep; own RadioServiceThread */
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
