#ifndef PB10A_BEEP_SERVICE_H
#define PB10A_BEEP_SERVICE_H

#include <stdint.h>

/* ArduboyTones-style sequence: frequency (Hz), duration (ms), repeated.
 * REST=0 is followed by a duration; END/REPEAT occupy one word each.
 * The high frequency bit is accepted but ignored (PB10A has one PWM pin).
 * Arrays must be statically retained and terminated within 64 note pairs.
 * Unlike ArduboyTones' 1/1024 s units, PB10A durations are exact ms.
 */
#define BEEP_TICK_HZ 1000000u
#define BEEP_REST 0u
#define BEEP_END 0x8000u
#define BEEP_REPEAT 0x8001u
#define BEEP_HIGH_VOLUME 0x8000u
#define BEEP_QUEUE_CAPACITY 8u
#define BEEP_SEQUENCE_MAX_STEPS 64u
#define BEEP_VOLUME_MAX 5u

/* Musical notes are frequencies in Hz (A4 = 440 Hz), not timer ARR. */
#define BEEP_C4 262u
#define BEEP_CS4 277u
#define BEEP_D4 294u
#define BEEP_DS4 311u
#define BEEP_E4 330u
#define BEEP_F4 349u
#define BEEP_FS4 370u
#define BEEP_G4 392u
#define BEEP_GS4 415u
#define BEEP_A4 440u
#define BEEP_AS4 466u
#define BEEP_B4 494u
#define BEEP_C5 523u
#define BEEP_CS5 554u
#define BEEP_D5 587u
#define BEEP_DS5 622u
#define BEEP_E5 659u
#define BEEP_F5 698u
#define BEEP_FS5 740u
#define BEEP_G5 784u
#define BEEP_GS5 831u
#define BEEP_A5 880u
#define BEEP_AS5 932u
#define BEEP_B5 988u
#define BEEP_C6 1047u
#define BEEP_CS6 1109u

/* The service owns TIM3_CH4. All calls are from the cooperative foreground.
 * Sequences are not copied: keep the const uint16_t array alive throughout
 * playback (normally static const in Flash). The queue holds only pointers.
 */
void beep_init(void);
uint8_t beep_enqueue(const uint16_t *sequence); /* FIFO for system sounds. */
void beep_play(const uint16_t *sequence);       /* Replace playback and queue. */
void beep_tone(uint16_t frequency_hz, uint16_t duration_ms); /* 0 ms = sustain. */
void beep_poll(uint32_t now_ms);
void beep_stop(void);
uint8_t beep_busy(void);
uint8_t beep_queue_count(void);
uint8_t beep_note_index(void); /* Zero-based index within the current sequence. */

void beep_set_key_enabled(uint8_t enabled);
uint8_t beep_get_key_enabled(void);
/* PWM volume level 0..5 (default maximum). Level zero cancels playback.
 * Levels 1..5 use approximate 6 dB steps of ideal fundamental power. */
void beep_set_volume(uint8_t volume);
uint8_t beep_get_volume(void);
void beep_key_click(void);
uint8_t beep_popup(const uint16_t *sequence);

#endif
