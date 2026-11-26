/* Minimal fatal-fault recorder for STM32G070 (also G071 compatible).
 * NEVER log by UART, OLED, I2C, malloc, printf, or Flash programming here.
 * IRQs are masked before writing; magic is stored last for atomic validity.
 *
 * The scatter file maps .bss.noinit to the last 256 bytes of 36KB G070 SRAM.
 * If SRAM parity is enabled through Option Bytes, SRAM shrinks to 32KB:
 * the scatter layout and startup parity initialization MUST be redesigned
 * first. Do NOT enable SRAM parity with the current 36KB scatter layout.
 */
#include "fault_record.h"
#include "main.h"
#include "millis.h"

#define FAULT_RECORD_MAGIC     0x50424631u /* "PBF1" */
#define FAULT_RECORD_VERSION 2u
#define WATCHDOG_TRACE_MAGIC 0x57444731u /* WDG1 */
#define FAULT_RECORD_XOR_SALT  0xA5F0231Du

/* Real .bss.noinit object prevents the linker reporting an unused RW_NOINIT.
 * 'used' also retains the symbol for debugger/watch inspection.
 * No initializer: C runtime must not touch this reserved SRAM area. */
volatile FaultRecord_t g_fault_record
    __attribute__((section(".bss.noinit"), used, aligned(4)));

volatile watchdog_trace_t g_watchdog_trace
    __attribute__((section(".bss.noinit"), used, aligned(4)));
volatile uint32_t g_watchdog_max_round_ms;
static uint32_t s_round_started, s_last_feed_tick;

static uint32_t fault_checksum(void) {
    return FAULT_RECORD_XOR_SALT ^ g_fault_record.version ^ g_fault_record.reason ^
           g_fault_record.exception ^ g_fault_record.flash_eccr ^
           g_fault_record.syscfg_cfgr2 ^ g_fault_record.rcc_csr ^
           g_fault_record.watchdog.magic ^ g_fault_record.watchdog.round ^
           g_fault_record.watchdog.tick ^
           ((uint32_t)g_fault_record.watchdog.thread |
            ((uint32_t)g_fault_record.watchdog.phase << 8u) |
            ((uint32_t)g_fault_record.watchdog.reserved << 16u));
}

uint8_t FaultRecord_IsValid(void) {
    /* Do not trust uninitialized SRAM across cold power-on. */
    return g_fault_record.magic == FAULT_RECORD_MAGIC &&
           g_fault_record.version == FAULT_RECORD_VERSION &&
           g_fault_record.checksum == fault_checksum();
}

static void fault_write(FaultReason_t reason, uint32_t exception, uint32_t rcc_csr) {
    g_fault_record.magic = 0u;
    g_fault_record.version = FAULT_RECORD_VERSION;
    g_fault_record.reason = (uint32_t)reason;
    g_fault_record.exception = exception;
    g_fault_record.flash_eccr = FLASH->ECCR;
    g_fault_record.syscfg_cfgr2 = SYSCFG->CFGR2;
    g_fault_record.rcc_csr = rcc_csr;
    watchdog_trace_t trace = {0};
    if (g_watchdog_trace.magic == WATCHDOG_TRACE_MAGIC)
        trace = g_watchdog_trace;
    g_fault_record.watchdog = trace;
    g_fault_record.checksum = fault_checksum();
    __DMB();
    g_fault_record.magic = FAULT_RECORD_MAGIC; /* Commit marker last. */
    __DSB();
}

void FaultRecord_BootInit(void) {
    uint32_t csr = RCC->CSR;
    if (csr & RCC_CSR_WWDGRSTF)
        fault_write(FAULT_REASON_WWDG, 0u, csr);
    /* Preserve the committed record, not the previous live trace. */
    g_watchdog_trace.magic = 0u;
    SET_BIT(RCC->CSR, RCC_CSR_RMVF);
}

__attribute__((noreturn)) void FaultRecord_Reset(FaultReason_t reason) {
    __disable_irq();
    fault_write(reason, __get_IPSR(), RCC->CSR);
    NVIC_SystemReset();
    for (;;) {
    } /* CMSIS reset request is expected not to return. */
}

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

__attribute__((noreturn)) void FaultRecord_UnhandledIRQ(void) {
    FaultRecord_Reset(FAULT_REASON_UNHANDLED_IRQ);
}
