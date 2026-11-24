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

static uint32_t s_boot_seconds;
static uint8_t s_boot_valid;

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
    /* Called once after MX_RTC_Init resets the calendar on every boot. */
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
    AddItem("-Time&Date",UI_ITEM_PARENTS,img_home,&s_time_item,parent,&s_time_page,0);
    AddPage("[Time&Date]",&s_time_page,UI_PAGE_TEXT,parent);
    AddItem("[Back]",UI_ITEM_RETURN,0,&s_back,&s_time_page,parent,0);
    for(i=0u;i<6u;i++)
        AddItem(labels[i],UI_ITEM_DATA,0,&s_fields[i],&s_time_page,0,0);
}
