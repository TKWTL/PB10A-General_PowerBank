#ifndef HOST_BEEP_H
#define HOST_BEEP_H
#include <stdint.h>
#define BEEP_C5 523u
#define BEEP_E5 659u
#define BEEP_G5 784u
#define BEEP_A5 880u
#define BEEP_C6 1047u
#define BEEP_END 0x8000u
uint8_t beep_busy(void);
uint8_t beep_enqueue(const uint16_t *sequence);
#endif
