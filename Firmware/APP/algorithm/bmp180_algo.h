#ifndef PB10A_BMP180_ALGO_H
#define PB10A_BMP180_ALGO_H
#include "ui_conf.h"
typedef struct {
    int32_t pressure_pa;
    int temperature_c10;
    uint32_t at_ms, seq;
} bmp180_sample_t;
uint8_t BMP180Algo_GetSample(bmp180_sample_t *out);
void BMP180Algo_Init(void);
void BMP180Algo_Poll(void);
void BMP180Algo_AddStatisticsItems(ui_page_t *page);
#endif
