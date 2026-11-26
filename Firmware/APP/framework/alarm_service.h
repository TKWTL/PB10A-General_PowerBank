#ifndef PB10A_ALARM_SERVICE_H
#define PB10A_ALARM_SERVICE_H

#include <stdint.h>
#include "ui_conf.h"

/* Two RTC-backed alarms, common effect settings and one ringing WORD page. */
void Alarm_Init(void);
void Alarm_Reschedule(void); /* Apply after RTC calendar edits. */
void Alarm_Poll(uint32_t now);
void Alarm_Stop(void);
uint8_t Alarm_IsRinging(void);
void Alarm_HandleAction(UI_ACTION action, uint8_t repeat);
ui_item_t *Alarm_RingItem(void);
void Add_Alarm_Items(ui_page_t *parent);

#endif
