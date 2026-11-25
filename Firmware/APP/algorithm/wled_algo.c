/* PB02B brightness mapping, thermal derating and SOS, adapted for PB10A. */
#include "wled_algo.h"
#include "power_algo.h"
#include "tim.h"

#define WLED_MIN_LEVEL 4u
#define WLED_DEFAULT_LEVEL 12u
#define WLED_MAX_LEVEL 25u
#define WLED_RAMP_MS 160u
#define WLED_MIN_PULSE (WLED_MIN_LEVEL * WLED_MIN_LEVEL)

static const uint8_t s_thermal_limit[] = {
    25,
    25,
    24,
    24,
    23,
    23,
    22,
    22,
    21,
    21,
    21,
    20,
    20,
    19,
    19,
    19,
    18,
    17,
    17,
    16,
    16,
    14,
    13,
    11,
    10,
    8,
    7,
    5,
    4,
    2,
    0,
};

static int8_t s_step_request;
static uint8_t s_sos_request, s_sos;
static uint8_t s_level, s_output_level, s_active;
static uint16_t s_pwm, s_ramp_from, s_target;
static uint32_t s_wait_at, s_ramp_at, s_sos_at;

static void apply_pwm(uint16_t pwm) {
    if (pwm != s_pwm) {
        WLED_SetPwm(pwm);
        s_pwm = pwm;
    }
}

void WLED_AlgoStop(void) {
    apply_pwm(0u);
    s_step_request = 0;
    s_sos_request = s_sos = 0u;
    s_level = s_output_level = s_active = 0u;
    s_target = 0u;
    PowerAlgo_ExtLoadRelease(EXT_LOAD_WLED);
}

void WLED_AlgoRequestStep(int8_t step, uint8_t repeat) {
    if (repeat && !s_level)
        return;
    int requested = s_step_request + step;
    if (requested >= -(int)WLED_MAX_LEVEL && requested <= (int)WLED_MAX_LEVEL)
        s_step_request = (int8_t)requested;
}

void WLED_AlgoRequestSOS(uint8_t enable) {
    s_sos_request = enable ? 1u : 2u;
}

uint8_t WLED_AlgoGetOutputPercent(void) {
    return (uint8_t)(s_output_level * 100u / WLED_MAX_LEVEL);
}

static void wled_request(uint32_t now) {
    if (!s_sos_request && !s_step_request)
        return;

    if (s_sos_request == 2u) {
        WLED_AlgoStop();
        return;
    }

    uint8_t next_level = s_level;
    if (s_sos_request == 1u) {
        next_level = WLED_MAX_LEVEL;
        s_sos = 1u;
        s_sos_at = now;
    } else if (!s_level && s_step_request > 0) {
        next_level = WLED_DEFAULT_LEVEL;
    } else if (s_level) {
        int requested = s_level + s_step_request;
        next_level = requested<(int)WLED_MIN_LEVEL ? 0u : requested>(int) WLED_MAX_LEVEL ? WLED_MAX_LEVEL : (uint8_t)requested;
    }

    s_sos_request = 0u;
    s_step_request = 0;
    if (!next_level) {
        WLED_AlgoStop();
        return;
    }
    if (!s_level) {
        if (!PowerAlgo_ExtLoadRequest(EXT_LOAD_WLED)) {
            WLED_AlgoStop();
            return;
        }
        s_wait_at = now;
    }
    s_level = next_level;
}

static uint16_t sos_pwm(uint32_t now, uint16_t peak) {
    /* Time in 100 ms units. The dash is three units long. */
    static const uint8_t pulse_start[] = {0, 2, 4, 8, 12, 16, 22, 24, 26};
    unsigned unit = (now - s_sos_at) % 5000u / 100u;
    for (unsigned i = 0u; i < sizeof(pulse_start); ++i) {
        unsigned width = (i >= 3u && i < 6u) ? 3u : 1u;
        if (unit >= pulse_start[i] && unit < pulse_start[i] + width)
            return peak;
    }
    return 0u;
}

void WLED_AlgoService(uint32_t now) {
    wled_request(now);
    if (!s_level)
        return;
    ext_load_state_t state = PowerAlgo_ExtLoadState(EXT_LOAD_WLED, now);
    if (state == EXT_LOAD_BLOCKED) {
        WLED_AlgoStop();
        return;
    }
    if (state == EXT_LOAD_WAIT) {
        if (s_active) {
            apply_pwm(0u);
            s_active = s_output_level = 0u;
            s_target = 0u;
            s_wait_at = now;
            PowerAlgo_ExtLoadSetActive(EXT_LOAD_WLED, 0u);
        } else if ((uint32_t)(now - s_wait_at) >= EXT_LOAD_WAIT_MS) {
            WLED_AlgoStop();
        }
        return;
    }

    int16_t c10 = PowerAlgo_ExtLoadTemperature();
    uint8_t limit = s_thermal_limit[c10 > 0 ? (unsigned)c10 / 20u : 0u];
    if (limit < WLED_MIN_LEVEL) {
        /* At 62.5 kHz, levels below 4 cannot reliably start EN/PWM. */
        WLED_AlgoStop();
        return;
    }
    uint8_t output_level = s_level < limit ? s_level : limit;
    uint16_t cap = (uint16_t)(limit * limit);
    uint16_t target = (uint16_t)(output_level * output_level);
    if (!s_active) {
        PowerAlgo_ExtLoadSetActive(EXT_LOAD_WLED, 1u);
        s_active = 1u;
        s_sos_at = now;
    }
    s_output_level = output_level;
    if (s_sos) {
        /* Every SOS pulse obeys the same instantaneous thermal limit. */
        apply_pwm(sos_pwm(now, target));
        return;
    }
    if (s_pwm > cap)
        apply_pwm(cap);
    if (target != s_target) {
        s_ramp_from = s_pwm;
        s_target = target;
        s_ramp_at = now;
    }
    uint32_t elapsed = now - s_ramp_at;
    if (elapsed >= WLED_RAMP_MS) {
        apply_pwm(s_target);
    } else {
        int32_t delta = (int32_t)s_target - s_ramp_from;
        uint16_t pwm = (uint16_t)(s_ramp_from + delta * (int32_t)elapsed / (int32_t)WLED_RAMP_MS);
        /* Keep the first high pulse at or above 250 ns. */
        apply_pwm(pwm < WLED_MIN_PULSE ? WLED_MIN_PULSE : pwm);
    }
}
