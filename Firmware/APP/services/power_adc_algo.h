#ifndef PB10A_POWER_ADC_ALGO_H
#define PB10A_POWER_ADC_ALGO_H

#include "ui_conf.h"
#include <stdint.h>

/* Sampling service for the auxiliary USB-A circuit.
 * CH0=PA0 SUB_IOUT; CH5=PA5 SUB_VOUT; both are 12-bit raw ADC.
 */
void PowerADC_Init(void);
void PowerADC_Poll(void);
void PowerADC_AddStatisticsItems(ui_page_t *page);

#endif
