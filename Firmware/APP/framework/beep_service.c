/* PB10A single-channel audio: Hz/ms sequence playback and a tone queue.
 * TIM3_CH4/PB1 hardware PWM, cooperative foreground timing, no waveform ISR.
 */
#include "beep_service.h"
#include "tim.h"
#include "fifo.h"

/**************************** Hardware PWM ****************************/

static uint8_t s_beep_pwm_running, s_volume;

static uint16_t beep_hw_get_ccr(uint16_t arr, uint8_t volume) {
    static const uint16_t duty_q15[BEEP_VOLUME_MAX + 1u] = {
        0u, 652u, 1307u, 2636u, 5461u, 16384u
    };
    uint32_t period = (uint32_t)arr + 1u;
    return (uint16_t)((period * duty_q15[volume] + 16384u) >> 15);
}

static void beep_hw_init(void) {
    s_beep_pwm_running = 0u;
    __HAL_TIM_SET_PRESCALER(&htim3, 63u);
    __HAL_TIM_SET_AUTORELOAD(&htim3, 999u);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0u);
    __HAL_TIM_SET_COUNTER(&htim3, 0u);
    htim3.Instance->EGR = TIM_EGR_UG;
}

static void beep_hw_stop(void) {
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, 0u);
    if (s_beep_pwm_running) {
        (void)HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_4);
        s_beep_pwm_running = 0u;
    }
}

static void beep_hw_set_frequency(uint16_t frequency_hz) {
    /* ArduboyTones' per-note high-volume flag is ignored on one PWM pin.
     * The supported pitch range is 16..32767 Hz (16-bit TIM3 at 1 MHz).
     */
    frequency_hz &= 0x7fffu;
    if (frequency_hz < 16u) {
        beep_hw_stop();
        return;
    }

    uint32_t period = (BEEP_TICK_HZ + (uint32_t)frequency_hz / 2u) / frequency_hz;
    __HAL_TIM_SET_AUTORELOAD(&htim3, period - 1u);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, beep_hw_get_ccr((uint16_t)(period - 1u), s_volume));
    __HAL_TIM_SET_COUNTER(&htim3, 0u);
    htim3.Instance->EGR = TIM_EGR_UG;
    if (!s_beep_pwm_running) {
        if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_4) == HAL_OK)
            s_beep_pwm_running = 1u;
        else
            beep_hw_stop();
    }
}

/************************* Cooperative playback ************************/

static const uint16_t s_key_sound[] = {2700u, 100u, BEEP_END};
static const uint16_t s_fault_sound[] = {
    2400u, 150u, BEEP_REST, 80u, 2400u, 150u, BEEP_REST, 80u, 2400u, 150u, BEEP_END};
static const uint16_t s_alarm_tone1[] = {
    2000u, 180u, BEEP_REST, 100u, 2000u, 180u, BEEP_REST, 540u, BEEP_REPEAT};
static const uint16_t s_alarm_tone2[] = {
    BEEP_C5, 180u, BEEP_E5, 180u, BEEP_G5, 360u, BEEP_REST, 280u, BEEP_REPEAT};
static const uint16_t s_alarm_tone3[] = {
    1600u, 180u, 2400u, 180u, 1600u, 180u, 2400u, 180u, BEEP_REST, 280u, BEEP_REPEAT};
static const uint16_t *const s_alarm_tones[] = {s_alarm_tone1, s_alarm_tone2, s_alarm_tone3};
/* PB10A is 32-bit. Queue stores one 4-byte pointer value per entry. */
#if defined(__arm__) || defined(__thumb__)
typedef char beep_pointer_must_be_32bit[(sizeof(const uint16_t *) == 4u) ? 1 : -1];
#endif
static uint8_t s_queue_storage[BEEP_QUEUE_CAPACITY * 4u];
static fifo_t s_queue;
static const uint16_t *s_current, *s_alarm_sequence;
static uint32_t s_step_started;
static uint16_t s_step_duration, s_word_position, s_tone_frequency, s_tone_duration;
static uint8_t s_note_index, s_key_enabled;
static uint8_t s_tone_active, s_need_note;

void beep_init(void) {
    (void)FIFO_Init(&s_queue, s_queue_storage, sizeof(s_queue_storage));
    s_note_index = 0u;
    s_current = s_alarm_sequence = 0;
    s_step_started = 0u;
    s_step_duration = s_word_position = s_tone_frequency = s_tone_duration = 0u;
    s_tone_active = s_need_note = 0u;
    s_key_enabled = 0u; /* Click Beep defaults OFF. */
    s_volume = BEEP_VOLUME_MAX;
    beep_hw_init();
    beep_hw_stop();
}

uint8_t beep_enqueue(const uint16_t *sequence) {
    if (s_alarm_sequence || s_current == s_fault_sound ||
        s_volume == 0u || !sequence || sequence[0] == BEEP_END ||
        sequence[0] == BEEP_REPEAT)
        return 0u;
    return FIFO_Push4(&s_queue, (uint32_t)(uintptr_t)sequence);
}

static void beep_clear(void) {
    FIFO_Clear(&s_queue);
    s_note_index = 0u;
    s_current = 0;
    s_tone_active = s_need_note = 0u;
    s_word_position = s_step_duration = 0u;
    beep_hw_stop();
}

uint8_t beep_fault_active(void) {
    return s_current == s_fault_sound;
}

void beep_stop(void) {
    s_alarm_sequence = 0;
    beep_clear();
}

void beep_fault(void) {
    if (!s_volume || beep_fault_active())
        return; /* Coalesce repeated notices while the finite cue plays. */
    beep_clear();
    s_current = s_fault_sound;
    s_need_note = 1u;
}

void beep_alarm(uint8_t enabled, unsigned melody) {
    const uint16_t *previous = s_alarm_sequence;
    s_alarm_sequence = enabled ? s_alarm_tones[melody] : 0;
    if (s_alarm_sequence != previous && !beep_fault_active())
        beep_clear();
}

void beep_play(const uint16_t *sequence) {
    if (s_alarm_sequence || beep_fault_active())
        return;
    beep_clear();
    if (!sequence || s_volume == 0u || sequence[0] == BEEP_END ||
        sequence[0] == BEEP_REPEAT)
        return;
    s_current = sequence;
    s_need_note = 1u; /* First note starts on the next 1 ms service poll. */
}

void beep_tone(uint16_t frequency_hz, uint16_t duration_ms) {
    if (s_alarm_sequence || beep_fault_active())
        return;
    beep_clear();
    if (s_volume == 0u || (frequency_hz == 0u && duration_ms == 0u))
        return;
    s_tone_frequency = frequency_hz;
    s_tone_duration = duration_ms;
    s_tone_active = s_need_note = 1u;
}

__attribute__((noinline)) void beep_poll(uint32_t now_ms) {
    if (!s_volume)
        return;
    if (s_tone_active) {
        if (s_need_note) {
            beep_hw_set_frequency(s_tone_frequency);
            s_step_started = now_ms;
            s_step_duration = s_tone_duration;
            s_need_note = 0u;
            return;
        }
        if (s_step_duration == 0u ||
            (uint32_t)(now_ms - s_step_started) < s_step_duration)
            return;
        s_tone_active = 0u;
        beep_hw_stop();
    }

    if (s_current && !s_need_note) {
        if (s_step_duration == 0u ||
            (uint32_t)(now_ms - s_step_started) < s_step_duration)
            return;
        s_need_note = 1u;
    }

    /* At most one audible note per poll. END may advance the FIFO and
     * REPEAT may rewind the score; neither burns a playback note slot.
     */
    for (uint8_t attempt = 0u; attempt <= BEEP_QUEUE_CAPACITY + 2u; ++attempt) {
        if (!s_current) {
            if (s_alarm_sequence) {
                s_current = s_alarm_sequence;
            } else {
                uint32_t address;
                if (!FIFO_Pop4(&s_queue, &address))
                    return;
                s_current = (const uint16_t *)(uintptr_t)address;
            }
            s_word_position = 0u;
            s_note_index = 0u;
            s_need_note = 1u;
        }

        uint16_t word = s_current[s_word_position];
        if (word == BEEP_END) {
            s_current = 0;
            s_need_note = 0u;
            beep_hw_stop();
            continue;
        }
        if (word == BEEP_REPEAT) {
            if (s_word_position == 0u) {
                s_current = 0; /* Empty repeat: no playable notes. */
                s_need_note = 0u;
                beep_hw_stop();
            } else {
                s_word_position = 0u;
                s_note_index = 0u;
            }
            continue;
        }
        if (s_word_position >= BEEP_SEQUENCE_MAX_STEPS * 2u) {
            s_current = 0;
            s_need_note = 0u;
            beep_hw_stop();
            continue;
        }

        s_note_index = (uint8_t)(s_word_position / 2u);
        s_step_duration = s_current[s_word_position + 1u];
        s_word_position += 2u;
        beep_hw_set_frequency(word);
        s_step_started = now_ms;
        s_need_note = 0u;
        return;
    }
}

uint8_t beep_busy(void) {
    return (uint8_t)(s_alarm_sequence != 0 || s_current != 0 || s_tone_active ||
                     FIFO_Count(&s_queue) != 0u);
}

uint8_t beep_queue_count(void) {
    return (uint8_t)(FIFO_Count(&s_queue) / 4u);
}

uint8_t beep_note_index(void) {
    return s_note_index;
}

void beep_set_key_enabled(uint8_t enabled) {
    s_key_enabled = enabled ? 1u : 0u;
}

uint8_t beep_get_key_enabled(void) {
    return s_key_enabled;
}

void beep_set_volume(uint8_t volume) {
    if (volume > BEEP_VOLUME_MAX)
        volume = BEEP_VOLUME_MAX;
    if (s_volume == volume)
        return;

    s_volume = volume;
    if (volume == 0u) {
        beep_clear(); /* Keep the alarm request; silence every priority. */
    } else if (s_beep_pwm_running) {
        uint16_t arr = (uint16_t)__HAL_TIM_GET_AUTORELOAD(&htim3);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, beep_hw_get_ccr(arr, volume));
    }
}

uint8_t beep_get_volume(void) {
    return s_volume;
}

void beep_key_click(void) {
    if (s_key_enabled && s_volume != 0u && !beep_busy())
        (void)beep_enqueue(s_key_sound);
}

uint8_t beep_popup(const uint16_t *sequence) {
    return beep_enqueue(sequence);
}
