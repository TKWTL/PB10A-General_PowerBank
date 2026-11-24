#include "mini_math.h"

uint32_t isqrt_u32(uint32_t value) {
    uint32_t root = 0u;
    uint32_t bit = 1u << 30;
    while (bit > value) bit >>= 2;
    while (bit) {
        if (value >= root + bit) {
            value -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* atan(i / 64) in centidegrees, rounded to the nearest node value. */
static const uint16_t atan_table[65] = {
    0u, 90u, 179u, 268u, 358u, 447u, 536u, 624u,
    713u, 800u, 888u, 975u, 1062u, 1148u, 1234u, 1319u,
    1404u, 1488u, 1571u, 1653u, 1735u, 1817u, 1897u, 1977u,
    2056u, 2134u, 2211u, 2287u, 2363u, 2438u, 2511u, 2584u,
    2657u, 2728u, 2798u, 2867u, 2936u, 3003u, 3070u, 3136u,
    3201u, 3264u, 3327u, 3390u, 3451u, 3511u, 3571u, 3629u,
    3687u, 3744u, 3800u, 3855u, 3909u, 3963u, 4016u, 4067u,
    4119u, 4169u, 4218u, 4267u, 4315u, 4363u, 4409u, 4455u,
    4500u,
};

int16_t atan2_cdeg(int32_t y, int32_t x) {
    uint32_t ax = x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
    uint32_t ay = y < 0 ? 0u - (uint32_t)y : (uint32_t)y;
    uint32_t high = ax >= ay ? ax : ay;
    uint32_t low = ax >= ay ? ay : ax;
    if (high == 0u) return 0;

    /* Keep the Q15 ratio calculation within 32 bits for any input.
     * The leveler's scaled vector is already inside this range. */
    while (high > 65535u) {
        high >>= 1;
        low >>= 1;
    }
    uint32_t ratio = ((low << 15) + high / 2u) / high;
    uint32_t index = ratio >> 9;
    int32_t angle = atan_table[index];
    if (index < 64u) {
        uint32_t step = (uint32_t)(atan_table[index + 1u] - atan_table[index]);
        angle += (int32_t)((step * (ratio & 511u) + 256u) >> 9);
    }
    if (ay > ax) angle = 9000 - angle;
    if (x < 0) angle = 18000 - angle;
    return (int16_t)(y < 0 ? -angle : angle);
}
