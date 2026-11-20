#include "millis.h"

/* Incremented directly in the 1 ms SysTick interrupt, independent of HAL. */
volatile uint32_t millis;

uint32_t Millis(void) {
    return millis;
}

void Millis_Delay(uint32_t duration_ms) {
    const uint32_t start = Millis();
    while ((uint32_t)(Millis() - start) < duration_ms) {
        /* IRQs remain enabled so SysTick continues to advance. */
    }
}
