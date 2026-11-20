#ifndef PB10A_RADIO_APP_H
#define PB10A_RADIO_APP_H
#include "ui_conf.h"
/* Nonblocking Collar-Radio-style five-item menu and FM control. */
void Radio_AppRun(ui_t *ui);
/* Boot: enqueue ENABLE=0; MCO is disabled only after IRQ completion.
 * Returns 1 if the asynchronous request was accepted. */
uint8_t Radio_AppInitLowPower(void);
void Radio_AppStart(void);
void Radio_AppStop(void);
uint8_t Radio_AppStopPending(void);
void Radio_AppPoll(void);
void Radio_AppDraw(ui_t *ui);
uint8_t Radio_AppIsActive(void);
uint8_t Radio_AppIsReady(void);
uint8_t Radio_AppIsSearching(void);
#endif
