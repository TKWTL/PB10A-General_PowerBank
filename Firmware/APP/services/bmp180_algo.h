#ifndef PB10A_BMP180_ALGO_H
#define PB10A_BMP180_ALGO_H
#include "ui_conf.h"
void BMP180Algo_Init(void);
void BMP180Algo_Poll(void);
void BMP180Algo_AddStatisticsItems(ui_page_t *page);
#endif
