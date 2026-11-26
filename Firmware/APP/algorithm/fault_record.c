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

#define FAULT_RECORD_MAGIC     0x50424631u /* "PBF1" */
#define FAULT_RECORD_VERSION 2u
#define FAULT_RECORD_XOR_SALT  0xA5F0231Du

/* Real .bss.noinit object prevents the linker reporting an unused RW_NOINIT.
 * 'used' also retains the symbol for debugger/watch inspection.
 * No initializer: C runtime must not touch this reserved SRAM area. */
volatile FaultRecord_t g_fault_record
    __attribute__((section(".bss.noinit"), used, aligned(4)));

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
    Watchdog_CopyTrace(&g_fault_record.watchdog);
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
    Watchdog_ClearTrace();
    SET_BIT(RCC->CSR, RCC_CSR_RMVF);
}

__attribute__((noreturn)) void FaultRecord_Reset(FaultReason_t reason) {
    __disable_irq();
    fault_write(reason, __get_IPSR(), RCC->CSR);
    NVIC_SystemReset();
    for (;;) {
    } /* CMSIS reset request is expected not to return. */
}

__attribute__((noreturn)) void FaultRecord_UnhandledIRQ(void) {
    FaultRecord_Reset(FAULT_REASON_UNHANDLED_IRQ);
}
