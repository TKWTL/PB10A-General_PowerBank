#ifndef CORO_OS_PORT_H_
#define CORO_OS_PORT_H_

#include "stm32g0xx.h"

/* Ordinary Sleep between polling rounds. SysTick and peripheral IRQs
 * drive the next round; deep sleep configuration belongs to the platform. */
static inline void CoroOS_PortIdle(void) {
    if (SCB->SCR & SCB_SCR_SLEEPDEEP_Msk)
        return;
    if (__get_PRIMASK())
        return;
    if (!(SysTick->CTRL & SysTick_CTRL_TICKINT_Msk))
        return;

    __DSB();
    __WFI();
}

#endif
