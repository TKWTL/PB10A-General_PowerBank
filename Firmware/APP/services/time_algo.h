#ifndef PB10A_TIME_ALGO_H
#define PB10A_TIME_ALGO_H

#include <stdint.h>
#include <stddef.h>

/* RTC-based time and uptime; RTC calendar is retained across reset by
 * PB10A backup-domain validity marker. No timezone/DST synchronization. */
typedef struct
{
    uint32_t year;     /* 2000..2099 */
    uint32_t month;    /* 1..12 */
    uint32_t day;      /* 1..31, calendar-validated */
    uint32_t hour;     /* 0..23 */
    uint32_t minute;   /* 0..59 */
    uint32_t second;   /* 0..59 */
} Time_Calendar;

typedef struct
{
    uint32_t days;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} Time_Duration;

void Time_Init(void);
uint8_t Time_ReadCalendar(Time_Calendar *out);
uint8_t Time_CalendarToSeconds(const Time_Calendar *calendar,
                               uint32_t *seconds);
uint8_t Time_GetUptimeSeconds(uint32_t *seconds);
void Time_SplitDuration(uint32_t seconds, Time_Duration *out);
void Time_FormatDuration(uint32_t seconds, char *out, size_t size);

#endif
