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

/* Standard pressure altitude (101325 Pa), integer meter nodes at 1 kPa. */
// clang-format off
static const int16_t altitude_lut[81] = {
    PRESSURE_ALT_MAX_M, 8945, 8731, 8522, 8318, 8119, 7924, 7734, 7547, 7365,
    7187, 7012, 6840, 6672, 6507, 6345, 6185, 6029, 5875, 5724,
    5575, 5429, 5285, 5143, 5004, 4866, 4730, 4597, 4465, 4335,
    4207, 4081, 3956, 3833, 3711, 3591, 3473, 3356, 3240, 3126,
    3013, 2901, 2791, 2681, 2573, 2467, 2361, 2256, 2153, 2051,
    1949, 1849, 1750, 1651, 1554, 1458, 1362, 1267, 1174, 1081,
    989, 897, 807, 717, 629, 540, 453, 366, 281, 195,
    111, 27, -56, -139, -220, -302, -382, -462, -541, -620,
    PRESSURE_ALT_MIN_M,
};
// clang-format on

uint8_t pressure_altitude_dm(int32_t pa, int32_t *out) {
    if (!out || pa < PRESSURE_ALT_MIN_PA || pa > PRESSURE_ALT_MAX_PA)
        return 0u;
    unsigned offset = (unsigned)(pa - PRESSURE_ALT_MIN_PA);
    unsigned index = offset / 1000u;
    int32_t dm = (int32_t)altitude_lut[index] * 10;
    if (index < 80u) {
        int32_t drop = altitude_lut[index] - altitude_lut[index + 1u];
        dm -= (drop * (int32_t)(offset % 1000u) + 50) / 100;
    }
    *out = dm;
    return 1u;
}

uint8_t altitude_pressure_pa(int32_t dm, int32_t *out) {
    if (!out || dm < PRESSURE_ALT_MIN_M * 10 || dm > PRESSURE_ALT_MAX_M * 10)
        return 0u;

    /* Locate the two enclosing nodes of the descending altitude table. */
    unsigned low = 0u, high = 80u;
    while (high - low > 1u) {
        unsigned mid = (low + high) / 2u;
        if (dm > (int32_t)altitude_lut[mid] * 10)
            high = mid;
        else
            low = mid;
    }
    int32_t drop_m = altitude_lut[low] - altitude_lut[high];
    int32_t delta_dm = (int32_t)altitude_lut[low] * 10 - dm;
    *out = PRESSURE_ALT_MIN_PA + (int32_t)low * 1000 +
           (delta_dm * 100 + drop_m / 2) / drop_m;
    return 1u;
}

/* Caller resets count on a rejected span. No per-sample buffer is needed. */
__attribute__((noinline)) uint8_t axis_capture_add(axis_capture_t *capture, const int values[3], int32_t span) {
    for (unsigned i = 0u; i < 3u; ++i) {
        int32_t v = values[i];
        if (!capture->count) {
            capture->sum[i] = 0;
            capture->min[i] = capture->max[i] = v;
        } else {
            if (v < capture->min[i])
                capture->min[i] = v;
            if (v > capture->max[i])
                capture->max[i] = v;
            if (capture->max[i] - capture->min[i] > span)
                return 0u;
        }
        capture->sum[i] += v;
    }
    ++capture->count;
    return 1u;
}

__attribute__((noinline)) void axis_capture_mean(const axis_capture_t *capture, int16_t mean[3]) {
    for (unsigned i = 0u; i < 3u; ++i)
        mean[i] = (int16_t)(capture->sum[i] / (int32_t)capture->count);
}
