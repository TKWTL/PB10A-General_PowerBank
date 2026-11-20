/* Minimal fatal-fault recorder for STM32G070 (also G071 compatible).
* NEVER log by UART, OLED, I2C, malloc, printf, or Flash programming here.
* IRQs are masked before writing; magic is stored last for atomic validity.
*
* The scatter file maps .noinit to the last 256 bytes of 36KB G070 SRAM.
* If SRAM parity is enabled through Option Bytes, SRAM shrinks to 32KB:
* the scatter layout and startup parity initialization MUST be redesigned
* first. Do NOT enable SRAM parity with the current 36KB scatter layout.
*/
#include "fault_record.h"
#include "main.h"

#define FAULT_RECORD_MAGIC     0x50424631u /* "PBF1" */
#define FAULT_RECORD_VERSION   1u
#define FAULT_RECORD_XOR_SALT  0xA5F0231Du

/* Real .noinit object prevents the linker reporting an unused RW_NOINIT.
* 'used' also retains the symbol for debugger/watch inspection.
* No initializer: C runtime must not touch this reserved SRAM area. */
volatile FaultRecord_t g_fault_record
__attribute__((section(".noinit"), used, aligned(4)));

static uint32_t fault_checksum(uint32_t version, uint32_t reason,
    uint32_t exception, uint32_t flash_eccr,
    uint32_t syscfg_cfgr2, uint32_t rcc_csr) {
    return FAULT_RECORD_XOR_SALT ^ version ^ reason ^ exception ^
    flash_eccr ^ syscfg_cfgr2 ^ rcc_csr;
}

uint8_t FaultRecord_IsValid(void) {
    /* Do not trust uninitialized SRAM across cold power-on. On devices
    * with SRAM parity enabled even the first read needs parity setup. */
    if(g_fault_record.magic != FAULT_RECORD_MAGIC ||
        g_fault_record.version != FAULT_RECORD_VERSION)
    return 0u;
    return (uint8_t)(g_fault_record.checksum ==
        fault_checksum(g_fault_record.version,g_fault_record.reason,
        g_fault_record.exception,g_fault_record.flash_eccr,
        g_fault_record.syscfg_cfgr2,g_fault_record.rcc_csr));
}

__attribute__((noreturn)) void FaultRecord_Reset(FaultReason_t reason) {
    uint32_t flash_eccr, syscfg_cfgr2, rcc_csr, exception;
    __disable_irq();
    /* Capture hardware state before software reset clears status bits. */
    flash_eccr=FLASH->ECCR;
    syscfg_cfgr2=SYSCFG->CFGR2;
    rcc_csr=RCC->CSR;
    exception=__get_IPSR();
    g_fault_record.magic=0u; /* Previous snapshot is no longer valid. */
    g_fault_record.version=FAULT_RECORD_VERSION;
    g_fault_record.reason=(uint32_t)reason;
    g_fault_record.exception=exception;
    g_fault_record.flash_eccr=flash_eccr;
    g_fault_record.syscfg_cfgr2=syscfg_cfgr2;
    g_fault_record.rcc_csr=rcc_csr;
    g_fault_record.checksum=fault_checksum(FAULT_RECORD_VERSION,
        (uint32_t)reason,exception,flash_eccr,syscfg_cfgr2,rcc_csr);
    __DMB();
    g_fault_record.magic=FAULT_RECORD_MAGIC; /* Commit marker last. */
    __DSB();
    NVIC_SystemReset();
    for(;;) {} /* CMSIS reset request is expected not to return. */
}

__attribute__((noreturn)) void FaultRecord_UnhandledIRQ(void) {
    FaultRecord_Reset(FAULT_REASON_UNHANDLED_IRQ);
}
