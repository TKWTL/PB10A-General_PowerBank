#ifndef PB10A_WLED_ALGO_H
#define PB10A_WLED_ALGO_H

#include <stdint.h>

void WLED_AlgoRequestStep(int8_t step, uint8_t repeat);
void WLED_AlgoRequestSOS(uint8_t enable);
void WLED_AlgoService(uint32_t now);
void WLED_AlgoStop(void); /* PM: clear pending requests and drive PWM low. */
uint8_t WLED_AlgoGetOutputPercent(void);

#endif
