#ifndef PB10A_BEEP_HW_H
#define PB10A_BEEP_HW_H
#include <stdint.h>
/* PWM backend for TIM3_CH4/PB1. Host tests mock these calls. */
void beep_hw_init(void);
void beep_hw_set_arr(uint16_t arr); /* zero mutes output */
void beep_hw_stop(void);
#endif
