#ifndef PB10A_SYSTEM_ALGO_H
#define PB10A_SYSTEM_ALGO_H
#include "ui_conf.h"
void System_Reset(void);
void System_AddSettingsItems(ui_page_t *page);
void System_AddStatisticsItems(ui_page_t *page);
void System_Poll(void);
/* RAM-only automatic page navigation switches; default disabled. */
uint8_t System_AutoRadioNavigationEnabled(void);
uint8_t System_AutoPowerbankNavigationEnabled(void);
#endif
