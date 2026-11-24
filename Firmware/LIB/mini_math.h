#ifndef PB10A_MINI_MATH_H
#define PB10A_MINI_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t isqrt_u32(uint32_t value);
/* Principal angle in 0.01 degrees, in [-18000, 18000]; (0, 0) gives 0. */
int16_t atan2_cdeg(int32_t y, int32_t x);

#ifdef __cplusplus
}
#endif

#endif
