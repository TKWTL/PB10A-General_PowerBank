#ifndef PB10A_RADIO_APP_H
#define PB10A_RADIO_APP_H
#include "ui_conf.h"
/* Collar-Radio style nonblocking WORD page. The UI consumes RAM snapshots
 * and queues algorithm commands; no I2C, radio state machine or PM here. */
extern const uint8_t s_battery_icon[16];
void Radio_AppRun(ui_t *ui);
void Radio_AppDraw(ui_t *ui);
void Radio_AppOnLeave(ui_t *ui);
#endif
