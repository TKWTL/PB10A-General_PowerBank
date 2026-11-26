#ifndef CORO_OS_H_
#define CORO_OS_H_

#include <stdint.h>
#include "millis.h"
#include "pt.h"
#include "pt-sem.h"

/* Optional PB10A watchdog/trace hooks; disabled for other ports. */
#ifndef COROOS_USE_WATCHDOG
#define COROOS_USE_WATCHDOG 0
#endif

/* Optional interrupt-driven polling: sleep once after every full round.
 * Platforms enabling this must provide CoroOS_PortIdle() in coroOS_port.h. */
#ifndef COROOS_USE_IDLE_SLEEP
#define COROOS_USE_IDLE_SLEEP 0
#endif

/* Handheld-Fan-style coroOS wrapper over Adam Dunkels' Protothreads 1.4.
 * Stackless coroutines do not retain automatic locals through a yield.
 * The single sub-PT per coroutine is used sequentially, never in parallel.
 * All THRD_DELAY/THRD_SPAWN waits yield to other registered coroOS tasks.
 */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct pt coro_pt_t;
typedef char (*coro_thread_fn_t)(coro_pt_t *pt);

typedef struct {
    const coro_thread_fn_t *threads;
    coro_pt_t *states;
    uint8_t count;
} coro_scheduler_t;

enum {
    CORO_WAITING = PT_WAITING,
    CORO_YIELDED = PT_YIELDED,
    CORO_ENDED = PT_ENDED
};

void CoroOS_Init(coro_scheduler_t *scheduler, const coro_thread_fn_t *threads, coro_pt_t *states, uint8_t count);
void CoroOS_RunOnce(coro_scheduler_t *scheduler);

/*************************** coroOS / Protothreads ***************************/
#define CORO_INIT(pt)             PT_INIT(pt)
#define THRD_DECLARE(name)        static char name(coro_pt_t *pt)
#define THRD_BEGIN                static uint32_t _coro_deadline; static struct pt _coro_subpt; PT_BEGIN(pt)
#define THRD_YIELD                PT_YIELD(pt)
#define THRD_UNTIL(cond)          PT_WAIT_UNTIL(pt, (cond))
#define THRD_WHILE(cond)          PT_WAIT_WHILE(pt, (cond))
#define THRD_DELAY(ms)            do { _coro_deadline = Millis() + (uint32_t)(ms); PT_WAIT_UNTIL(pt, (int32_t)(Millis() - _coro_deadline) >= 0); } while (0)
#define THRD_SPAWN_NOARG(func)    PT_SPAWN(pt, &_coro_subpt, func(&_coro_subpt))
#define THRD_SPAWN_ARGS(func, ...) PT_SPAWN(pt, &_coro_subpt, func(&_coro_subpt, __VA_ARGS__))
#define THRD_END                  PT_END(pt)

#ifdef __cplusplus
}
#endif
#endif
