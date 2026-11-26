/* PB10A unified RTC service: calendar calculations, uptime and
 * MiaoUI aligned calendar editor, adapted from IH01A and Collar-Radio.
 * keep the safe field-by-field copy of HAL RTC's packed time/date,
 * preserve the aligned wall-clock editing UI and RTC arithmetic,
 * without the original FM/FreeRTOS dependencies or float conversions.
 *
 * All HAL_RTC_GetTime calls MUST be followed by HAL_RTC_GetDate to
 * unlock shadow registers. Only called from cooperative foreground.
 */
#include "time_service.h"
#include "rtc.h"
#include "core/ui.h"
#include "images/image.h"
#include "mini_format.h"
#include "beep_service.h"
#include "pm_api.h"
#include "millis.h"
#include "i18n.h"
#include "display/dispDriver.h"
#include "indev/indevDriver.h"

static uint32_t s_boot_seconds;
static uint8_t s_boot_valid;
static void alarm_reschedule_all(void);

static uint8_t Time_DaysInMonth(uint32_t year, uint32_t month) {
    static const uint8_t days[12] =
    {31u,28u,31u,30u,31u,30u,31u,31u,30u,31u,30u,31u};
    uint8_t result;
    if(month < 1u || month > 12u) return 0u;
    result = days[month - 1u];
    if(month == 2u && ((year & 3u) == 0u)) result = 29u;
    return result;
}

uint8_t Time_CalendarToSeconds(const Time_Calendar *calendar,
    uint32_t *seconds) {
    uint32_t y, m, day_of_year = 0u;
    if(calendar == 0 || seconds == 0 ||
        calendar->year < 2000u || calendar->year > 2099u ||
        calendar->month < 1u || calendar->month > 12u ||
        calendar->day < 1u ||
        calendar->day > Time_DaysInMonth(calendar->year, calendar->month) ||
        calendar->hour > 23u || calendar->minute > 59u ||
        calendar->second > 59u) return 0u;

    for(m=1u; m<calendar->month; ++m)
        day_of_year += Time_DaysInMonth(calendar->year, m);
    day_of_year += calendar->day - 1u;
    y = calendar->year - 2000u;
    /* Leap years: 2000, 2004...2096; 2100 excluded by validation. */
    *seconds = ((y * 365u + (y + 3u)/4u + day_of_year)*86400u) +
    calendar->hour*3600u +
    calendar->minute*60u + calendar->second;
    return 1u;
}

uint8_t Time_ReadCalendar(Time_Calendar *out) {
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};
    if(out == 0) return 0u;
    if(HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return 0u;
    /* Mandatory paired read of RTC date after RTC time. */
    if(HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) return 0u;
    out->year = 2000u + (uint32_t)d.Year;
    out->month = (uint32_t)d.Month;
    out->day = (uint32_t)d.Date;
    out->hour = (uint32_t)t.Hours;
    out->minute = (uint32_t)t.Minutes;
    out->second = (uint32_t)t.Seconds;
    return 1u;
}

void Time_Init(void) {
    Time_Calendar now;
    s_boot_valid = 0u;
    s_boot_seconds = 0u;
    /* Called once after MX_RTC_Init restores or initializes the calendar. */
    if(Time_ReadCalendar(&now) &&
        Time_CalendarToSeconds(&now, &s_boot_seconds))
    s_boot_valid = 1u;
}

uint8_t Time_GetUptimeSeconds(uint32_t *seconds) {
    Time_Calendar now;
    uint32_t value;
    if(seconds == 0 || !s_boot_valid) return 0u;
    if(!Time_ReadCalendar(&now) ||
        !Time_CalendarToSeconds(&now, &value)) return 0u;
    if(value < s_boot_seconds) return 0u;
    *seconds = value - s_boot_seconds;
    return 1u;
}

void Time_SplitDuration(uint32_t seconds, Time_Duration *out) {
    if(out == 0) return;
    out->days = seconds / 86400u;
    seconds %= 86400u;
    out->hours = (uint8_t)(seconds / 3600u);
    seconds %= 3600u;
    out->minutes = (uint8_t)(seconds / 60u);
    out->seconds = (uint8_t)(seconds % 60u);
}

void Time_FormatDuration(uint32_t seconds, char *out, size_t size) {
    Time_Duration d;
    if(out == 0 || size == 0u) return;
    Time_SplitDuration(seconds, &d);
    (void)mini_snprintf(out, size, "%ud %02u:%02u:%02u",
        (unsigned)d.days, (unsigned)d.hours,
        (unsigned)d.minutes, (unsigned)d.seconds);
}

/******************** MiaoUI calendar edit interface ********************/
RTC_TimeTypeDef_Aligned sTime;
static uint8_t s_rtc_read_valid;

uint8_t RTC_CacheValid(void) {
    return s_rtc_read_valid;
}
void RTC_UpdateTime(void) {
    RTC_TimeTypeDef t={0};
    RTC_DateTypeDef d={0};
    s_rtc_read_valid = 0u;
    if(HAL_RTC_GetTime(&hrtc,&t,RTC_FORMAT_BIN)!=HAL_OK)return;
    if(HAL_RTC_GetDate(&hrtc,&d,RTC_FORMAT_BIN)!=HAL_OK)return;
    sTime.Year=d.Year;sTime.Month=d.Month;sTime.Date=d.Date;
    sTime.WeekDay=d.WeekDay;sTime.Hours=t.Hours;
    sTime.Minutes=t.Minutes;sTime.Seconds=t.Seconds;
    s_rtc_read_valid = d.Year <= 99u && d.Date >= 1u &&
                       d.Date <= Time_DaysInMonth(2000u + d.Year, d.Month) &&
                       t.Hours < 24u && t.Minutes < 60u && t.Seconds < 60u;
}

static uint8_t days_in_month(int y,int m) {
    static const uint8_t ds[12]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(m<1 || m>12)return 0u;
    if(m==2 && (y%4)==0)return 29u; /* 2000-2099 */
    return ds[m-1];
}

static uint8_t weekday(int y,int m,int day) {
    /* RTC_WEEKDAY_MONDAY=1...SUNDAY=7; 2000-01-01 was Saturday. */
    int d=5,yy,mm;
    for(yy=0;yy<y;yy++)d+=365+((yy%4)==0);
    for(mm=1;mm<m;mm++)d+=days_in_month(y,mm);
    d+=day-1;
    return (uint8_t)(d%7+1);
}

void SetTime(ui_t *ui) {
    RTC_TimeTypeDef t={0};
    RTC_DateTypeDef d={0};
    (void)ui;
    if(sTime.Year<0 || sTime.Year>99 || sTime.Month<1 ||
        sTime.Month>12 || sTime.Date<1 ||
        sTime.Date>days_in_month(sTime.Year,sTime.Month) ||
        sTime.Hours<0 || sTime.Hours>23 ||
        sTime.Minutes<0 || sTime.Minutes>59 ||
        sTime.Seconds<0 || sTime.Seconds>59)return;
    d.Year=(uint8_t)sTime.Year;
    d.Month=(uint8_t)sTime.Month;
    d.Date=(uint8_t)sTime.Date;
    d.WeekDay=weekday(sTime.Year,sTime.Month,sTime.Date);
    t.Hours=(uint8_t)sTime.Hours;t.Minutes=(uint8_t)sTime.Minutes;
    t.Seconds=(uint8_t)sTime.Seconds;
    t.DayLightSaving=RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation=RTC_STOREOPERATION_RESET;
    s_rtc_read_valid = HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN) == HAL_OK &&
                       HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN) == HAL_OK;
    if (s_rtc_read_valid) {
        sTime.WeekDay = d.WeekDay;
        alarm_reschedule_all();
    }
}

static ui_item_t s_time_item,s_back,s_fields[6];
static ui_page_t s_time_page;
static ui_data_t s_data[6];
static ui_element_t s_element[6];
static const char *const s_names[6]={"Second","Minute","Hour","Date","Month","Year"};
uint8_t RTC_IsEditing(const ui_t *ui) {
    uint8_t i;
    if(!ui || (ui->menuState!=UI_ITEM_RUNING &&
        ui->menuState!=UI_ITEM_DRAWING))return 0u;
    for(i=0u;i<6u;i++)if(ui->nowItem==&s_fields[i])return 1u;
    return 0u;
}

void Create_TimeSet_Parameters(ui_t *ui) {
    int *ptr[6]={&sTime.Seconds,&sTime.Minutes,&sTime.Hours,
        &sTime.Date,&sTime.Month,&sTime.Year};
    const int hi[6]={59,59,23,31,12,99};
    const int lo[6]={0,0,0,1,1,0};
    uint8_t i;
    (void)ui;
    for(i=0u;i<6u;i++) {
        s_data[i].name=s_names[i];
        s_data[i].ptr=ptr[i];
        s_data[i].function=SetTime;
        s_data[i].functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
        s_data[i].dataType=UI_DATA_INT;
        s_data[i].actionType=UI_DATA_ACTION_RW;
        s_data[i].max=hi[i];s_data[i].min=lo[i];s_data[i].step=1;
        s_element[i].data=&s_data[i];
        Create_element(&s_fields[i],&s_element[i]);
    }
}

void Add_TimeSet_Items(ui_page_t *parent) {
    uint8_t i;
    static const char *const labels[6]={
        " Second"," Minute"," Hour"," Date"," Month"," Year"
    };
    AddItem("-Time & Date", UI_ITEM_PARENTS, img_home, &s_time_item, parent, &s_time_page, 0);
    AddPage("[Time & Date]", &s_time_page, UI_PAGE_TEXT, parent);
    AddItem("[Back]",UI_ITEM_RETURN,0,&s_back,&s_time_page,parent,0);
    for(i=0u;i<6u;i++)
        AddItem(labels[i],UI_ITEM_DATA,0,&s_fields[i],&s_time_page,0,0);
}


/************************ RTC calendar alarms ************************/
#define ALARM_COUNT 2u
#define ALARM_DAY_COUNT 7u
#define ALARM_TEST_BIT 0x04u
#define ALARM_BACKUP_TAG UINT32_C(0xA1000000)
#define ALARM_BACKUP_TAG_MASK UINT32_C(0xFF000000)

enum { ALARM_MELODY,
       ALARM_DURATION,
       ALARM_DISMISS,
       ALARM_EFFECT_COUNT };
enum { ALARM_ONE_MIN,
       ALARM_FIVE_MIN,
       ALARM_FOREVER };
enum { ALARM_PRESS,
       ALARM_SEQUENCE };

typedef struct {
    int hour, minute;
    uint8_t enabled, days[ALARM_DAY_COUNT]; /* Sun..Sat UI switch values. */
} alarm_config_t;

static alarm_config_t s_alarms[ALARM_COUNT];
static int s_effect[ALARM_EFFECT_COUNT] = {0, ALARM_FIVE_MIN, ALARM_PRESS};
static uint8_t s_rearm_mask, s_ringing_mask, s_ring_page_active, s_dismiss_step;
static uint32_t s_rearm_at, s_ring_started;

static uint32_t alarm_hw(unsigned index) {
    return index ? RTC_ALARM_B : RTC_ALARM_A;
}

static uint8_t alarm_week_mask(const alarm_config_t *cfg) {
    uint8_t mask = 0u;
    for (unsigned i = 0u; i < ALARM_DAY_COUNT; ++i)
        mask |= (uint8_t)((cfg->days[i] != 0u) << i);
    return mask;
}

static void alarm_save(void) {
    /* ALRMAR/ALRMBR retain the time. DR1 has only software-only fields:
     * week masks [6:0]/[13:7], melody [16:14], duration [18:17],
     * dismiss [19], enables [20:21], reserved [23:22], tag [31:24]. */
    uint32_t value = ALARM_BACKUP_TAG | alarm_week_mask(&s_alarms[0]) |
                     (uint32_t)alarm_week_mask(&s_alarms[1]) << 7u |
                     (uint32_t)s_effect[ALARM_MELODY] << 14u |
                     (uint32_t)s_effect[ALARM_DURATION] << 17u |
                     (uint32_t)s_effect[ALARM_DISMISS] << 19u |
                     (uint32_t)s_alarms[0].enabled << 20u |
                     (uint32_t)s_alarms[1].enabled << 21u;
    HAL_RTCEx_BKUPWrite(&hrtc, RTC_BKP_DR1, value);
}

/* Return STM32 weekday (Mon=1..Sun=7), strictly after current time.
 * A zero mask is one shot, today if future or tomorrow otherwise. */
static uint8_t alarm_next_weekday(uint8_t today, unsigned now_minute, unsigned target_minute, uint8_t mask) {
    unsigned offset = target_minute > now_minute ? 0u : 1u;
    for (; offset <= 7u; ++offset) {
        uint8_t day = (uint8_t)((today - 1u + offset) % 7u + 1u);
        if (!mask || (mask & (1u << (day % 7u))))
            return day;
    }
    return today;
}

static void alarm_read_time(unsigned index) {
    RTC_AlarmTypeDef alarm = {0};
    alarm_config_t *cfg = &s_alarms[index];
    if (HAL_RTC_GetAlarm(&hrtc, &alarm, alarm_hw(index), RTC_FORMAT_BIN) == HAL_OK &&
        alarm.AlarmTime.Hours < 24u && alarm.AlarmTime.Minutes < 60u) {
        cfg->hour = alarm.AlarmTime.Hours;
        cfg->minute = alarm.AlarmTime.Minutes;
    } else {
        cfg->hour = cfg->minute = 0;
        cfg->enabled = 0u;
    }
}

static void alarm_update_pm_block(void) {
    /* A same-second rearm may need one second before writing the next
     * weekly match; never suspend with that alarm temporarily disarmed. */
    pm_api_set_sleep_block(PM_SLEEP_BLOCK_ALARM, s_ringing_mask || s_rearm_mask);
}

static void alarm_clear_event(unsigned index) {
    __HAL_RTC_ALARM_CLEAR_FLAG(&hrtc, index ? RTC_FLAG_ALRBF : RTC_FLAG_ALRAF);
    pm_api_clear_rtc_alarm_events((uint8_t)(1u << index));
}

static uint8_t alarm_program(unsigned index) {
    alarm_config_t *cfg = &s_alarms[index];
    RTC_AlarmTypeDef alarm = {0};
    RTC_TimeTypeDef time;
    RTC_DateTypeDef date;
    uint8_t bit = (uint8_t)(1u << index);
    s_rearm_mask &= (uint8_t)~bit;
    alarm.Alarm = alarm_hw(index);
    /* Stop the old match before clearing its hardware/software IRQ.
     * The other alarm's pending bit and the independent WUT are untouched. */
    if (HAL_RTC_DeactivateAlarm(&hrtc, alarm.Alarm) != HAL_OK)
        return 0u;
    alarm_clear_event(index);
    if ((unsigned)cfg->hour >= 24u || (unsigned)cfg->minute >= 60u)
        return 0u;
    alarm.AlarmTime.Hours = (uint8_t)cfg->hour;
    alarm.AlarmTime.Minutes = (uint8_t)cfg->minute;
    alarm.AlarmSubSecondMask = RTC_ALARMSUBSECONDMASK_ALL;
    alarm.AlarmDateWeekDaySel = RTC_ALARMDATEWEEKDAYSEL_WEEKDAY;
    alarm.AlarmDateWeekDay = RTC_WEEKDAY_MONDAY;

    uint8_t defer = 0u;
    if (cfg->enabled) {
        if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK ||
            HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK ||
            date.WeekDay < 1u || date.WeekDay > 7u)
            return 0u;
        unsigned current = time.Hours * 60u + time.Minutes;
        unsigned target = (unsigned)cfg->hour * 60u + (unsigned)cfg->minute;
        alarm.AlarmDateWeekDay = alarm_next_weekday(date.WeekDay, current, target, alarm_week_mask(cfg));
        /* A single selected weekday repeats next week. Its register value
         * is identical to the match that just fired; do not re-enable
         * within the same matching second. */
        defer = alarm.AlarmDateWeekDay == date.WeekDay && current == target && time.Seconds == 0u;
        if (!defer)
            return HAL_RTC_SetAlarm_IT(&hrtc, &alarm, RTC_FORMAT_BIN) == HAL_OK;
    }

    /* Save hours/minutes in the RTC even when disabled. SetAlarm writes
     * without interrupt enable; deactivate immediately afterwards. */
    if (HAL_RTC_SetAlarm(&hrtc, &alarm, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_DeactivateAlarm(&hrtc, alarm.Alarm) != HAL_OK)
        return 0u;
    alarm_clear_event(index);
    if (defer) {
        s_rearm_mask |= bit;
        s_rearm_at = Millis() + 1000u;
    }
    return 1u;
}

static void alarm_apply(unsigned index) {
    if (!alarm_program(index)) {
        /* Do not claim a working alarm after an unsuccessful RTC write.
         * Keep the actual retained time instead of a fictitious edit. */
        s_alarms[index].enabled = 0u;
        alarm_read_time(index);
    }
    alarm_update_pm_block();
}

static void alarm_reschedule_all(void) {
    for (unsigned i = 0u; i < ALARM_COUNT; ++i)
        alarm_apply(i);
    alarm_save();
}

void Alarm_Init(void) {
    /* Older firmware initialized 2000-01-01 with a Monday weekday.
     * Repair a retained calendar once, before scheduling weekday matches. */
    RTC_UpdateTime();
    if (RTC_CacheValid()) {
        uint8_t day = weekday(sTime.Year, sTime.Month, sTime.Date);
        if (sTime.WeekDay != day) {
            RTC_DateTypeDef date = {
                .Year = (uint8_t)sTime.Year,
                .Month = (uint8_t)sTime.Month,
                .Date = (uint8_t)sTime.Date,
                .WeekDay = day};
            if (HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK)
                sTime.WeekDay = day;
        }
    }
    uint32_t value = HAL_RTCEx_BKUPRead(&hrtc, RTC_BKP_DR1);
    if ((value & ALARM_BACKUP_TAG_MASK) == ALARM_BACKUP_TAG && !(value & 0x00C00000u)) {
        unsigned melody = (value >> 14u) & 7u;
        unsigned duration = (value >> 17u) & 3u;
        if (melody < BEEP_ALARM_MELODY_COUNT && duration <= ALARM_FOREVER) {
            s_effect[ALARM_MELODY] = (int)melody;
            s_effect[ALARM_DURATION] = (int)duration;
            s_effect[ALARM_DISMISS] = (int)((value >> 19u) & 1u);
            for (unsigned i = 0u; i < ALARM_COUNT; ++i) {
                s_alarms[i].enabled = (uint8_t)((value >> (20u + i)) & 1u);
                for (unsigned day = 0u; day < ALARM_DAY_COUNT; ++day)
                    s_alarms[i].days[day] = (uint8_t)((value >> (i * 7u + day)) & 1u);
            }
        }
    }
    for (unsigned i = 0u; i < ALARM_COUNT; ++i)
        alarm_read_time(i);
    /* Startup has disabled both hardware alarms; discard any stale
     * boot IRQ latches before scheduling the next future match. */
    pm_api_clear_rtc_alarm_events(PM_RTC_ALARM_A | PM_RTC_ALARM_B);
    alarm_reschedule_all();
}

/************************ Shared ringing session ************************/
uint8_t Alarm_IsRinging(void) {
    return s_ringing_mask != 0u;
}

static void alarm_start(uint8_t mask, uint32_t now) {
    if (!s_ringing_mask)
        s_dismiss_step = 0u;
    s_ringing_mask |= mask;
    s_ring_started = now;
    alarm_update_pm_block();
    pm_api_refresh_idle();
    beep_alarm(1u, (unsigned)s_effect[ALARM_MELODY]);
}

void Alarm_Stop(void) {
    s_ringing_mask = 0u;
    beep_alarm(0u, 0u); /* Do not stop a preempting FAULT cue. */
    alarm_update_pm_block();
    pm_api_refresh_idle();
    indevClearPending();
}

void Alarm_Poll(uint32_t now) {
    if (pm_api_ui_should_block())
        return; /* RTC writes after PM has restored the system clock/tick. */

    uint8_t events = pm_api_take_rtc_alarm_events();
    uint8_t fired = 0u;
    for (unsigned i = 0u; i < ALARM_COUNT; ++i) {
        uint8_t bit = (uint8_t)(1u << i);
        if (!(events & bit) || !s_alarms[i].enabled)
            continue;
        fired |= bit;
        if (!alarm_week_mask(&s_alarms[i]))
            s_alarms[i].enabled = 0u;
        alarm_apply(i); /* Rearm next weekday or disable the completed one shot. */
    }
    if (fired) {
        alarm_save();
        alarm_start(fired, now);
        MiaoUi_RequestNavigate(UI_TARGET_ALARM);
    }
    if (s_rearm_mask && (int32_t)(now - s_rearm_at) >= 0) {
        uint8_t pending = s_rearm_mask;
        for (unsigned i = 0u; i < ALARM_COUNT; ++i)
            if (pending & (1u << i))
                alarm_apply(i);
        alarm_save();
    }
    if (s_ringing_mask && s_effect[ALARM_DURATION] != ALARM_FOREVER &&
        (uint32_t)(now - s_ring_started) >= (s_effect[ALARM_DURATION] == ALARM_ONE_MIN ? 60000u : 300000u))
        Alarm_Stop();
}

void Alarm_HandleAction(UI_ACTION action, uint8_t repeat) {
    static const uint8_t sequence[] = {
        UI_ACTION_UP, UI_ACTION_UP, UI_ACTION_DOWN, UI_ACTION_DOWN,
        UI_ACTION_MINUS, UI_ACTION_PLUS, UI_ACTION_MINUS, UI_ACTION_PLUS};
    if (!s_ring_page_active || !s_ringing_mask || repeat || action == UI_ACTION_NONE)
        return;
    if (s_effect[ALARM_DISMISS] == ALARM_PRESS) {
        Alarm_Stop();
        return;
    }
    if (action == sequence[s_dismiss_step]) {
        if (++s_dismiss_step == sizeof(sequence))
            Alarm_Stop();
    } else {
        s_dismiss_step = action == sequence[0] ? 1u : 0u;
    }
}

/************************ MiaoUI alarm menus ************************/
static ui_page_t s_alarm_config_page, s_effect_page;
static ui_item_t s_alarm_select[ALARM_COUNT], s_alarm_config_back, s_effect_item, s_effect_back, s_alarm_test;
static ui_item_t s_alarm_fields[3u + ALARM_DAY_COUNT], s_effect_fields[ALARM_EFFECT_COUNT];
static ui_data_t s_alarm_data[3u + ALARM_DAY_COUNT], s_effect_data[ALARM_EFFECT_COUNT];
static ui_element_t s_alarm_elements[3u + ALARM_DAY_COUNT], s_effect_elements[ALARM_EFFECT_COUNT];
static uint8_t s_alarm_selected;

ui_item_t *Alarm_RingItem(void) {
    return &s_alarm_test;
}

static void alarm_page_leave(ui_t *ui) {
    (void)ui;
    s_ring_page_active = 0u;
}

static void alarm_page_run(ui_t *ui) {
    if (!s_ring_page_active) {
        s_ring_page_active = 1u;
        indevClearPending();
        if (!s_ringing_mask)
            alarm_start(ALARM_TEST_BIT, Millis());
    }
    if (!s_ringing_mask) {
        ui->action = UI_ACTION_BACK;
        return;
    }
    ui->action = UI_ACTION_NONE;
    uint8_t foreground = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&foreground);
    Disp_SetFont(UI_FONT);
    static const char *const titles[] = {"ALARM TEST", "ALARM 1", "ALARM 2", "ALARM 1&2"};
    Disp_DrawCentered(12u, I18N_Text(titles[s_ringing_mask & 3u]), 2u);
    Disp_DrawLine(0u, 15u, UI_HOR_RES - 1u, 15u);
    /* The trimmed 24px font has digits but no colon. Draw it as two dots. */
    char time[12];
    Disp_SetFont(font_radio_h24w12);
    mini_snprintf(time, sizeof(time), "%02u", (unsigned)sTime.Hours);
    Disp_DrawStr(37u, 40u, time);
    Disp_DrawBox(63u, 25u, 2u, 2u);
    Disp_DrawBox(63u, 34u, 2u, 2u);
    mini_snprintf(time, sizeof(time), "%02u", (unsigned)sTime.Minutes);
    Disp_DrawStr(67u, 40u, time);
    Disp_SetFont(UI_FONT);
    if (s_effect[ALARM_DISMISS] == ALARM_PRESS) {
        Disp_DrawCentered(60u, I18N_Text("Any key: Exit"), 2u);
    } else {
        Disp_DrawStr(3u, 60u, I18N_Text("UUDDLRLR"));
        mini_snprintf(time, sizeof(time), "%u/8", (unsigned)s_dismiss_step);
        Disp_DrawStr(UI_HOR_RES - 4u - Disp_GetUTF8Width(time), 60u, time);
    }
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, 200u);
}

static void alarm_config_changed(ui_t *ui) {
    (void)ui;
    alarm_apply(s_alarm_selected);
    alarm_save();
}

static void alarm_effect_changed(ui_t *ui) {
    (void)ui;
    alarm_save();
}

/* Both links share one configuration page and its 10 DATA instances.
 * This runs before the first child-menu draw (also updates its parent). */
static void alarm_select_config(ui_t *ui) {
    s_alarm_selected = (uint8_t)(ui->nowItem == &s_alarm_select[1]);
    alarm_config_t *cfg = &s_alarms[s_alarm_selected];
    s_alarm_config_page.item.parent = ui->nowItem;
    s_alarm_config_page.pageName = s_alarm_selected ? "[Alarm 2]" : "[Alarm 1]";
    s_alarm_data[0].ptr = &cfg->enabled;
    s_alarm_data[1].ptr = &cfg->hour;
    s_alarm_data[2].ptr = &cfg->minute;
    for (unsigned i = 0u; i < ALARM_DAY_COUNT; ++i)
        s_alarm_data[3u + i].ptr = &cfg->days[i];
}

void Add_Alarm_Items(ui_page_t *parent) {
    static const char *const labels[] = {" Enable", " Hour", " Minute", " Sun", " Mon", " Tue", " Wed", " Thu", " Fri", " Sat"};
    static const char *const effect_labels[] = {" Melody", " Duration", " Dismiss"};
    static const char *const melodies[] = {"Tone 1", "Tone 2", "Tone 3"};
    static const char *const durations[] = {"1min", "5min", "Forever"};
    static const char *const dismiss[] = {"Press", "Sequence"};
    static const char *const *const options[] = {melodies, durations, dismiss};
    static const uint8_t maxima[] = {BEEP_ALARM_MELODY_COUNT - 1u, ALARM_FOREVER, ALARM_SEQUENCE};
    AddItem("-Alarm 1", UI_ITEM_PARENTS, 0, &s_alarm_select[0], parent, &s_alarm_config_page, alarm_select_config);
    AddItem("-Alarm 2", UI_ITEM_PARENTS, 0, &s_alarm_select[1], parent, &s_alarm_config_page, alarm_select_config);
    AddItem("-Effect", UI_ITEM_PARENTS, 0, &s_effect_item, parent, &s_effect_page, 0);
    AddItem("!Alarm Test", UI_ITEM_WORD, 0, &s_alarm_test, parent, 0, alarm_page_run);
    UI_SetLeaveHook(&s_alarm_test, alarm_page_leave);

    AddPage("[Alarm 1]", &s_alarm_config_page, UI_PAGE_TEXT, parent);
    AddItem("[Back]", UI_ITEM_RETURN, 0, &s_alarm_config_back, &s_alarm_config_page, parent, 0);
    for (unsigned i = 0u; i < 3u + ALARM_DAY_COUNT; ++i) {
        ui_data_t *data = &s_alarm_data[i];
        data->name = labels[i];
        data->function = alarm_config_changed;
        data->functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
        data->actionType = UI_DATA_ACTION_RW;
        data->dataType = i == 1u || i == 2u ? UI_DATA_INT : UI_DATA_SWITCH;
        if (data->dataType == UI_DATA_INT) {
            data->max = i == 1u ? 23 : 59;
            data->step = 1;
        }
        s_alarm_elements[i].data = data;
        Create_element(&s_alarm_fields[i], &s_alarm_elements[i]);
        AddItem(labels[i], UI_ITEM_DATA, 0, &s_alarm_fields[i], &s_alarm_config_page, 0, 0);
    }
    /* Bind on entry, before the page can be drawn. */
    AddPage("[Effect]", &s_effect_page, UI_PAGE_TEXT, parent);
    AddItem("[Back]", UI_ITEM_RETURN, 0, &s_effect_back, &s_effect_page, parent, 0);
    for (unsigned i = 0u; i < ALARM_EFFECT_COUNT; ++i) {
        ui_data_t *data = &s_effect_data[i];
        data->name = effect_labels[i];
        data->ptr = &s_effect[i];
        data->dataType = UI_DATA_ENUM;
        data->actionType = UI_DATA_ACTION_RW;
        data->function = alarm_effect_changed;
        data->functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
        data->max = maxima[i];
        data->step = 1;
        data->options = options[i];
        s_effect_elements[i].data = data;
        Create_element(&s_effect_fields[i], &s_effect_elements[i]);
        AddItem(effect_labels[i], UI_ITEM_DATA, 0, &s_effect_fields[i], &s_effect_page, 0, 0);
    }
}
