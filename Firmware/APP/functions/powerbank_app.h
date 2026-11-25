#ifndef PB10A_POWERBANK_APP_H
#define PB10A_POWERBANK_APP_H
#include "ui_conf.h"
void Powerbank_AppRun(ui_t *ui);
uint8_t Powerbank_AppIsRunning(const ui_t *ui);
void Powerbank_AppDimKeys(uint8_t held, uint32_t now);
void Powerbank_AppOnLeave(ui_t *ui);
#endif
