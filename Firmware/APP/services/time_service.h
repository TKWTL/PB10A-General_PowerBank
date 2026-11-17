#ifndef PB10A_TIME_SERVICE_H
#define PB10A_TIME_SERVICE_H
#include "ui_conf.h"
/* M0+ safe aligned edit fields. Calendar year is 2000 + Year (0..99). */
typedef struct {
    int Seconds,Minutes,Hours,WeekDay,Date,Month,Year;
} RTC_TimeTypeDef_Aligned;
extern RTC_TimeTypeDef_Aligned sTime;
void RTC_UpdateTime(void);
void SetTime(ui_t *ui);
void Create_TimeSet_Parameters(ui_t *ui);
void Add_TimeSet_Items(ui_page_t *parent);
#endif
