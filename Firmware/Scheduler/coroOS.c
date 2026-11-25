#include "coroOS.h"

#if COROOS_USE_IDLE_SLEEP
#include "coroOS_port.h"
#endif

void CoroOS_Init(coro_scheduler_t *scheduler, const coro_thread_fn_t *threads, coro_pt_t *states, uint8_t count) {
    uint8_t i;
    if (!scheduler || !threads || !states || count == 0u) return;
    scheduler->threads = threads;
    scheduler->states = states;
    scheduler->count = count;
    for (i = 0u; i < count; ++i) PT_INIT(&states[i]);
}

void CoroOS_RunOnce(coro_scheduler_t *scheduler) {
    uint8_t i;
    if (!scheduler || !scheduler->threads || !scheduler->states || !scheduler->count) return;
    for (i = 0u; i < scheduler->count; ++i) (void)scheduler->threads[i](&scheduler->states[i]);

#if COROOS_USE_IDLE_SLEEP
    CoroOS_PortIdle();
#endif
}
