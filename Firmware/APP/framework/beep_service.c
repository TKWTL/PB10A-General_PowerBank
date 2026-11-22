/* PB10A base beep service: local TIM3_CH4/PB1 PWM implementation
 * followed by a nonblocking melody queue and tone sequencer.
 * Notes contain ARR values, not Hz; no software waveform or busy wait.
 */
#include "beep_service.h"

/********************** Hardware PWM backend ***********************/
/* The dedicated beep_hw module has been merged here. Only this
 * service owns the TIM3 PWM channel; all hardware helpers are private.
 * TIM3=64 MHz, PSC=63 => 1 MHz time base. */
#include "tim.h"
static uint8_t s_beep_pwm_running, s_volume;

static uint16_t beep_hw_get_ccr(uint16_t arr, uint8_t volume) {
    static const uint16_t duty_q15[BEEP_VOLUME_MAX + 1u] = {
        0u, 652u, 1307u, 2636u, 5461u, 16384u
    };
    uint32_t period = (uint32_t)arr + 1u;
    return (uint16_t)((period * duty_q15[volume] + 16384u) >> 15);
}
static void beep_hw_init(void) {
    s_beep_pwm_running=0u;
    __HAL_TIM_SET_PRESCALER(&htim3,63u);
    __HAL_TIM_SET_AUTORELOAD(&htim3,999u);
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,0u);
    __HAL_TIM_SET_COUNTER(&htim3,0u);
    htim3.Instance->EGR=TIM_EGR_UG;
}

static void beep_hw_stop(void) {
    __HAL_TIM_SET_COMPARE(&htim3,TIM_CHANNEL_4,0u);
    if(s_beep_pwm_running){(void)HAL_TIM_PWM_Stop(&htim3,TIM_CHANNEL_4);s_beep_pwm_running=0u;}
}

static void beep_hw_set_arr(uint16_t arr) {
    if(arr==0u){beep_hw_stop();return;}
    __HAL_TIM_SET_AUTORELOAD(&htim3,arr);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, beep_hw_get_ccr(arr, s_volume));
    __HAL_TIM_SET_COUNTER(&htim3,0u);
    htim3.Instance->EGR=TIM_EGR_UG;
    if(!s_beep_pwm_running) {
        if(HAL_TIM_PWM_Start(&htim3,TIM_CHANNEL_4)==HAL_OK)s_beep_pwm_running=1u;
        else beep_hw_stop();
    }
}


/********************** Cooperative sound queue ********************/

static const beep_note_t s_key_sound[] = {
    {BEEP_ARR_HZ(2700u),100u}, BEEP_END
};
static const beep_note_t *s_queue[BEEP_QUEUE_CAPACITY];
static const beep_note_t *s_current;
static uint32_t s_step_started;
static uint8_t s_head,s_tail,s_count,s_index,s_key_enabled;

void beep_init(void) {
    s_head=s_tail=s_count=s_index=0u;
    s_current=0;s_step_started=0u;
    s_key_enabled=0u; /* Click Beep: default OFF */
    s_volume = BEEP_VOLUME_MAX; /* Beep Volume: default maximum */
    beep_hw_init();
    beep_hw_stop();
}

uint8_t beep_enqueue(const beep_note_t *sequence) {
    if(s_volume == 0u || !sequence || sequence[0].duration_ms==0u ||
        s_count>=BEEP_QUEUE_CAPACITY)return 0u;
    s_queue[s_head]=sequence;
    s_head=(uint8_t)((s_head+1u)%BEEP_QUEUE_CAPACITY);
    ++s_count;
    return 1u;
}

void beep_poll(uint32_t now_ms) {
    if(s_current) {
        if((uint32_t)(now_ms-s_step_started)<
            s_current[s_index].duration_ms)return;
        ++s_index;
    }
    /* A clip may terminate just as the next clip becomes available.
     * Limit the loop even for malformed/empty sequence data. */
    for(uint8_t n=0u;n<=BEEP_QUEUE_CAPACITY;++n) {
        if(!s_current) {
            /* Output was muted when the preceding clip ended; avoid
             * touching TIM3 on every idle superloop iteration. */
            if(!s_count)return;
            s_current=s_queue[s_tail];
            s_tail=(uint8_t)((s_tail+1u)%BEEP_QUEUE_CAPACITY);
            --s_count;
            s_index=0u;
        }
        if(s_index>=BEEP_SEQUENCE_MAX_STEPS ||
            s_current[s_index].duration_ms==0u) {
            s_current=0;
            beep_hw_stop();
            continue;
        }
        beep_hw_set_arr(s_current[s_index].arr);
        s_step_started=now_ms;
        return;
    }
}

void beep_stop(void) {
    s_head=s_tail=s_count=s_index=0u;
    s_current=0;
    beep_hw_stop();
}

uint8_t beep_busy(void){return (uint8_t)(s_current!=0 || s_count!=0u);}
uint8_t beep_queue_count(void){return s_count;}
uint8_t beep_note_index(void){return s_index;}
void beep_set_key_enabled(uint8_t enabled){s_key_enabled=enabled?1u:0u;}
uint8_t beep_get_key_enabled(void){return s_key_enabled;}
void beep_set_volume(uint8_t volume) {
    if (volume > BEEP_VOLUME_MAX) volume = BEEP_VOLUME_MAX;
    if (s_volume == volume) return;

    s_volume = volume;
    if (volume == 0u) {
        beep_stop();
    } else if (s_beep_pwm_running) {
        uint16_t arr = (uint16_t)__HAL_TIM_GET_AUTORELOAD(&htim3);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_4, beep_hw_get_ccr(arr, volume));
    }
}

uint8_t beep_get_volume(void) {
    return s_volume;
}
void beep_key_click(void) {
    /* Don't flood the queue with auto-repeat or delay a popup melody. */
    if(s_key_enabled && s_volume != 0u && !beep_busy())
        (void)beep_enqueue(s_key_sound);
}

uint8_t beep_popup(const beep_note_t *sequence) {
    return beep_enqueue(sequence);
}
