#ifndef PB10A_POWER_ADC_ALGO_H
#define PB10A_POWER_ADC_ALGO_H

#include "ui_conf.h"
#include <stdint.h>

/* Software-triggered ADC1 scan (CH0/CH5/Temp/VREFINT), one DMA buffer per scan.
 * The caller is a coroOS task that waits for completion and delays 500 ms.
 */
void PowerADC_Init(void);
uint8_t PowerADC_BeginScan(void);
uint8_t PowerADC_ScanSettled(void);
uint8_t PowerADC_IsSuspended(void);
void PowerADC_AddStatisticsItems(ui_page_t *page);

#endif
