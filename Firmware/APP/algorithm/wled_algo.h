#ifndef PB10A_WLED_ALGO_H
#define PB10A_WLED_ALGO_H

#include <stdint.h>

/* Thermal imaging: measure the board at the full PWM range. The common
 * external-load overtemperature cutoff remains active. */
#ifndef WLED_ENABLE_THERMAL_DERATING
#define WLED_ENABLE_THERMAL_DERATING 0
#endif

void WLED_AlgoRequestStep(int8_t step, uint8_t repeat);
void WLED_AlgoRequestSOS(uint8_t enable);
void WLED_AlgoService(uint32_t now);
void WLED_AlgoStop(void); /* PM: clear pending requests and drive PWM low. */
uint8_t WLED_AlgoGetOutputPercent(void);

#endif
