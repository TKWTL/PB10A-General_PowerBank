#ifndef PB10A_WLED_ALGO_H
#define PB10A_WLED_ALGO_H

#include <stdint.h>

#define WLED_MANUAL_MAX_LEVEL 19u
#define WLED_PEAK_LEVEL 32u

/* SOS power budget in milliwatts. */
#ifndef WLED_SOS_PEAK_MW
#define WLED_SOS_PEAK_MW 15000u
#endif
#ifndef WLED_SOS_AVG_MW
#define WLED_SOS_AVG_MW 2500u
#endif

/* Optional calibrated hotspot sensor hook, returning nonzero for a valid
 * int16_t temperature in 0.1 C: WLED_READ_HOTSPOT_C10(out).
 * Without it MCU/NTC are ambient references, NOT hotspot measurements;
 * the measured steady-state rise envelope controls continuous output. */

typedef enum {
    WLED_SOS_OFF,
    WLED_SOS_WAIT_METER,
    WLED_SOS_TRANSMIT,
    WLED_SOS_COOLDOWN,
    WLED_SOS_PROTECTED
} wled_sos_state_t;

void WLED_AlgoRequestStep(int8_t step, uint8_t repeat);
void WLED_AlgoRequestSOS(uint8_t enable);
void WLED_AlgoService(uint32_t now);
void WLED_AlgoStop(void); /* PM: clear pending requests and drive PWM low. */
uint8_t WLED_AlgoGetOutputPercent(void);
wled_sos_state_t WLED_AlgoGetSOSStatus(uint8_t *progress);
uint8_t WLED_AlgoThermalBlocked(void);

#endif
