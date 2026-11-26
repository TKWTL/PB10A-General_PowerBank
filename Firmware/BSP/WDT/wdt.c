#include "wdt.h"
#include "stm32g0xx.h"
#include "stm32g0xx_ll_bus.h"
#include "millis.h"

#define WATCHDOG_TRACE_MAGIC 0x57444731u /* WDG1 */

volatile watchdog_trace_t g_watchdog_trace
    __attribute__((section(".bss.noinit"), used, aligned(4)));
volatile uint32_t g_watchdog_max_round_ms;
static uint32_t s_round_started, s_last_feed_tick;

/* PB10A WWDG: at PCLK=64MHz, 128 divider and 0x7F reload give
 * 64 * 4096 * 128 / PCLK = 524.288ms nominal. Window disabled.
 * No IWDG, LSI or Option Byte programming. */
static void watchdog_feed(uint32_t now) {
    WWDG->CR = WWDG_CR_WDGA | 0x7Fu;
    s_last_feed_tick = now;
}

static void watchdog_phase(watchdog_phase_t phase, uint8_t thread) {
    g_watchdog_trace.tick = Millis();
    g_watchdog_trace.thread = thread;
    g_watchdog_trace.phase = (uint8_t)phase;
}

void Watchdog_Init(void) {
    g_watchdog_trace.magic = 0u;
    g_watchdog_trace.round = 0u;
    g_watchdog_trace.reserved = 0u;
    g_watchdog_max_round_ms = 0u;
    watchdog_phase(WATCHDOG_PHASE_INIT, 0xFFu);
    __DMB();
    g_watchdog_trace.magic = WATCHDOG_TRACE_MAGIC;
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_WWDG);
    LL_APB1_GRP1_EnableClockStopSleep(LL_APB1_GRP1_PERIPH_WWDG);
    WWDG->CFR = WWDG_CFR_WDGTB | 0x7Fu;
    watchdog_feed(Millis());
}

void Watchdog_BeginRound(void) {
    ++g_watchdog_trace.round;
    watchdog_phase(WATCHDOG_PHASE_I2C, 0xFFu);
    s_round_started = g_watchdog_trace.tick;
}

void Watchdog_Thread(uint8_t index) {
    watchdog_phase(WATCHDOG_PHASE_THREAD, index);
}

void Watchdog_EndRound(void) {
    uint32_t now = Millis();
    uint32_t elapsed = now - s_round_started;
    if (elapsed > g_watchdog_max_round_ms)
        g_watchdog_max_round_ms = elapsed;
    /* Require a completed round AND a progressing SysTick. Do not feed
     * in the SysTick IRQ or inside individual coroutines. */
    if (now != s_last_feed_tick)
        watchdog_feed(now);
    watchdog_phase(WATCHDOG_PHASE_IDLE, 0xFFu);
}

void Watchdog_BeforeStop(void) {
    watchdog_phase(WATCHDOG_PHASE_STOP, g_watchdog_trace.thread);
    watchdog_feed(g_watchdog_trace.tick); /* reserve full wake margin */
}

void Watchdog_AfterStop(void) {
    watchdog_phase(WATCHDOG_PHASE_RESUME, g_watchdog_trace.thread);
}

void Watchdog_CopyTrace(volatile watchdog_trace_t *out) {
    watchdog_trace_t trace = {0};
    if (g_watchdog_trace.magic == WATCHDOG_TRACE_MAGIC)
        trace = g_watchdog_trace;
    *out = trace;
}

void Watchdog_ClearTrace(void) {
    g_watchdog_trace.magic = 0u;
}
