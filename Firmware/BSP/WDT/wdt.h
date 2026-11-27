#ifndef PB10A_WDT_H
#define PB10A_WDT_H

#include <stdint.h>

typedef enum {
    WATCHDOG_PHASE_INIT = 1u,
    WATCHDOG_PHASE_I2C,
    WATCHDOG_PHASE_THREAD,
    WATCHDOG_PHASE_IDLE,
    WATCHDOG_PHASE_STOP,
    WATCHDOG_PHASE_RESUME
} watchdog_phase_t;

/* Live, best-effort execution trace; thread 0xFF means no thread yet. */
typedef struct {
    uint32_t magic;
    uint32_t round;
    uint32_t tick;
    uint8_t thread;
    uint8_t phase;
    uint16_t reserved;
} watchdog_trace_t;

extern volatile watchdog_trace_t g_watchdog_trace;
extern volatile uint32_t g_watchdog_max_round_ms; /* awake tick time; excludes idle/STOP */

void Watchdog_Init(void);
void Watchdog_BeginRound(void);
void Watchdog_Thread(uint8_t index);
void Watchdog_EndRound(void);
/* Pair around actual STOP entry; ordinary scheduler Sleep stays monitored. */
void Watchdog_BeforeStop(void);
void Watchdog_AfterStop(void);

/* Access the retained execution trace without exposing record internals. */
void Watchdog_CopyTrace(volatile watchdog_trace_t *out);
void Watchdog_ClearTrace(void);

#endif
