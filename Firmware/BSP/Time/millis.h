#ifndef PB10A_MILLIS_H
#define PB10A_MILLIS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /* Independent 1 ms monotonically increasing counter, maintained ONLY by
    * SysTick_Handler in the CubeMX USER CODE region.
    *
    * uint32_t reads are atomic on Cortex-M0+ when naturally aligned.
    * It wraps in approximately 49.7 days; use unsigned elapsed subtraction.
    * SysTick stops during STOP1, so this measures awake CPU time rather
    * than wall-clock time. Use the RTC for sleep-inclusive elapsed time.
    */
    extern volatile uint32_t millis;

    /* Application clock, independent of HAL_GetTick() / HAL_IncTick(). */
    uint32_t Millis(void);

    /* Blocking delay for legacy device drivers; requires enabled SysTick IRQ.
    * Never call from an ISR or while SysTick is suspended for STOP1. */
    void Millis_Delay(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* PB10A_MILLIS_H */
