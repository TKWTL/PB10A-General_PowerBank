#include "beep_service.h"
#include "fifo.h"
#include "tim.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
TIM_TypeDef tim3_regs;
TIM_HandleTypeDef htim3 = {&tim3_regs};
static const uint16_t a[] = {440u,2u,BEEP_END};
static const uint16_t b[] = {880u,3u,BEEP_END};
int main(void) {
    fifo_t f; uint8_t storage[16]={0}; uintptr_t in=(uintptr_t)0x12345678u,out=0u;
    assert(FIFO_Init(&f,storage,16u));
    assert(FIFO_WriteExact(&f,&in,sizeof(in)));
    assert(FIFO_WriteExact(&f,&in,sizeof(in)));
    assert(!FIFO_WriteExact(&f,&in,sizeof(in)));
    assert(FIFO_ReadExact(&f,&out,sizeof(out)) && out==in);
    assert(FIFO_ReadExact(&f,&out,sizeof(out)) && out==in);
    assert(!FIFO_ReadExact(&f,&out,sizeof(out)));
    beep_init();
    assert(beep_queue_count()==0u && !beep_busy());
    for (uint8_t i=0;i<BEEP_QUEUE_CAPACITY;i++)assert(beep_enqueue(i&1u?a:b));
    assert(!beep_enqueue(a) && beep_queue_count()==BEEP_QUEUE_CAPACITY);
    beep_poll(1u);
    assert(beep_queue_count()==BEEP_QUEUE_CAPACITY-1u);
    beep_stop();
    assert(!beep_busy() && beep_queue_count()==0u);
    beep_play(a);
    assert(beep_busy() && beep_queue_count()==0u);
    beep_poll(10u);beep_poll(12u);beep_poll(13u);
    assert(!beep_busy());
    assert(beep_enqueue(a));
    beep_tone(600u,5u);
    assert(beep_queue_count()==0u);
    beep_poll(20u);beep_poll(25u);
    assert(!beep_busy());
    assert(beep_enqueue(a));
    beep_set_volume(0u);
    assert(!beep_busy() && !beep_enqueue(a));
    puts("beep fifo PASS");
    return 0;
}
