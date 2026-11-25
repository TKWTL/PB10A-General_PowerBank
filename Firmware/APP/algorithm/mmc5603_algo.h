#ifndef PB10A_MMC5603_ALGO_H
#define PB10A_MMC5603_ALGO_H
#include "ui_conf.h"
typedef struct {
    int mg[3]; /* native MMC5603 axes, milligauss */
    uint32_t at_ms, seq;
} mmc5603_sample_t;
uint8_t MMC5603Algo_GetSample(mmc5603_sample_t *out);
void MMC5603Algo_SetToolMode(uint8_t enable);
void MMC5603Algo_Init(void);
void MMC5603Algo_Poll(void);
void MMC5603Algo_AddStatisticsItems(ui_page_t *page);
#endif
