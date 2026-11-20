#ifndef PB10A_RADIO_APP_H
#define PB10A_RADIO_APP_H
#include "ui_conf.h"
/* Nonblocking Collar-Radio-style five-item menu and FM control. */
void Radio_AppRun(ui_t *ui);
/* Boot: enforce ENABLE=0 and disable the LSE-derived MCO afterwards.
 * Returns 1 only if the I2C power-down command was acknowledged. */
uint8_t Radio_AppInitLowPower(void);
void Radio_AppStart(void);
void Radio_AppStop(void);
void Radio_AppPoll(void);
void Radio_AppDraw(ui_t *ui);
uint8_t Radio_AppIsActive(void);
uint8_t Radio_AppIsReady(void);
uint8_t Radio_AppIsSearching(void);
#endif
