#ifndef PB10A_POWER_ALGO_H
#define PB10A_POWER_ALGO_H
#include <stdint.h>
#include "ui_conf.h"
/* SW6306 plus auxiliary A-port PA6 and ADC1 DMA telemetry. */
void PowerAlgo_Init(void);
uint8_t PowerAlgo_AdcIsSuspended(void);
uint8_t PowerAlgo_AdcBeginScan(void);
uint8_t PowerAlgo_AdcScanSettled(void);
void PowerAlgo_AddStatisticsItems(ui_page_t *page);
#endif
