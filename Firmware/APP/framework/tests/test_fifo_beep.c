#include "beep_service.h"
#include "fifo.h"
#include "tim.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

TIM_TypeDef tim3_regs;
TIM_HandleTypeDef htim3 = {&tim3_regs};
static const uint16_t a[] = {440u, 2u, BEEP_END};
static const uint16_t b[] = {880u, 3u, BEEP_END};

static void test_widths(void) {
    fifo_t f;
    uint8_t storage[8] = {0}, one;
    uint16_t two;
    uint32_t four;
    assert(FIFO_Init(&f, storage, sizeof(storage)));
    assert(FIFO_Push1(&f, 0xabu));
    assert(FIFO_Push2(&f, 0x1234u));
    assert(FIFO_Push4(&f, 0x87654321u));
    assert(!FIFO_Push2(&f, 0u)); /* All-or-nothing. */
    assert(FIFO_Count(&f) == 7u);
    assert(FIFO_Pop1(&f, &one) && one == 0xabu);
    assert(FIFO_Pop2(&f, &two) && two == 0x1234u);
    assert(FIFO_Pop4(&f, &four) && four == 0x87654321u);
    assert(!FIFO_Pop1(&f, &one));
    assert(FIFO_Push4(&f, 0xdeadbeefu)); /* Wrapped store. */
    assert(FIFO_Pop4(&f, &four) && four == 0xdeadbeefu);
    assert(FIFO_Push1(&f, 0x31u));
    assert(FIFO_Pop1(&f, &one) && one == 0x31u);
    FIFO_Clear(&f);
    assert(FIFO_Count(&f) == 0u);
}
int main(void) {
    test_widths();
    /* On x86_64 host, a non-PIE test binary gives low-address static notes.
     * On STM32, all pointers are naturally 32-bit without this host trick. */
    assert((uintptr_t)a <= UINT32_MAX && (uintptr_t)b <= UINT32_MAX);
    beep_init();
    assert(!beep_busy() && beep_queue_count() == 0u);
    for (uint8_t i = 0u; i < BEEP_QUEUE_CAPACITY; ++i)
        assert(beep_enqueue(i & 1u ? a : b));
    assert(!beep_enqueue(a));
    assert(beep_queue_count() == BEEP_QUEUE_CAPACITY);
    beep_poll(1u);
    assert(beep_queue_count() == BEEP_QUEUE_CAPACITY - 1u);
    beep_stop();
    assert(!beep_busy() && beep_queue_count() == 0u);
    beep_play(a);
    beep_poll(10u); beep_poll(12u); beep_poll(13u);
    assert(!beep_busy());
    assert(beep_enqueue(a));
    beep_tone(600u, 5u);
    assert(beep_queue_count() == 0u);
    beep_poll(20u); beep_poll(25u);
    assert(!beep_busy());
    assert(beep_enqueue(b));
    beep_set_volume(0u);
    assert(!beep_busy() && !beep_enqueue(a));
    puts("beep 1/2/4 byte FIFO PASS");
    return 0;
}
