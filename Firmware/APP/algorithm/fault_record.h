#ifndef PB10A_FAULT_RECORD_H
#define PB10A_FAULT_RECORD_H
#include <stdint.h>
#include "wdt.h"

/* Snapshot retained over software/WWDG reset in the scatter-file
 * RW_NOINIT area. Not persistent over power loss or firmware downloads.
 */
typedef enum {
    FAULT_REASON_NMI = 1u,
    FAULT_REASON_HARDFAULT = 2u,
    FAULT_REASON_ERROR_HANDLER = 3u,
    FAULT_REASON_UNHANDLED_IRQ = 4u,
    FAULT_REASON_WWDG = 5u
} FaultReason_t;

typedef struct {
    uint32_t magic;         /* Written LAST; zero while record is being built. */
    uint32_t version;
    uint32_t reason;
    uint32_t exception;     /* IPSR: 2=NMI, 3=HardFault, 16+=external IRQ. */
    uint32_t flash_eccr;    /* FLASH_ECCR_ECCD/ECCC and failing word offset. */
    uint32_t syscfg_cfgr2;  /* SYSCFG_CFGR2_SPF records SRAM parity failure. */
    uint32_t rcc_csr;
    watchdog_trace_t watchdog; /* Snapshot captured on WWDG/fatal reset. */
    uint32_t checksum; /* XOR of snapshot fields + salt. */
} FaultRecord_t;

extern volatile FaultRecord_t g_fault_record;
void FaultRecord_BootInit(void);
uint8_t FaultRecord_IsValid(void);
__attribute__((noreturn)) void FaultRecord_Reset(FaultReason_t reason);
__attribute__((noreturn)) void FaultRecord_UnhandledIRQ(void);

#endif
