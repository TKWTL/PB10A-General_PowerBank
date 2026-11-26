#ifndef PB10A_MINI_MATH_H
#define PB10A_MINI_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t isqrt_u32(uint32_t value);
/* Principal angle in 0.01 degrees, in [-18000, 18000]; (0, 0) gives 0. */
int16_t atan2_cdeg(int32_t y, int32_t x);
/* Range of the standard-atmosphere interpolation table. */
#define PRESSURE_ALT_MIN_PA 30000
#define PRESSURE_ALT_MAX_PA 110000
#define PRESSURE_ALT_MIN_M (-698)
#define PRESSURE_ALT_MAX_M 9165

/* Forward and inverse interpolation. Return 0 outside the table. */
uint8_t pressure_altitude_dm(int32_t pa, int32_t *out);
uint8_t altitude_pressure_pa(int32_t dm, int32_t *out);

#ifdef __cplusplus
}
#endif

#endif
