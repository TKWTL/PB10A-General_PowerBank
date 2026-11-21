/* Collar-Radio style RTC time/date editing, adapted to PB10A HAL RTC.
 * Never directly cast packed HAL RTC structures to MiaoUI int pointers.
 */
#include "time_service.h"
#include "rtc.h"
#include "core/ui.h"
#include "images/image.h"
RTC_TimeTypeDef_Aligned sTime;
void RTC_UpdateTime(void) {
    RTC_TimeTypeDef t={0};
    RTC_DateTypeDef d={0};
    if(HAL_RTC_GetTime(&hrtc,&t,RTC_FORMAT_BIN)!=HAL_OK)return;
    if(HAL_RTC_GetDate(&hrtc,&d,RTC_FORMAT_BIN)!=HAL_OK)return;
    sTime.Year=d.Year;sTime.Month=d.Month;sTime.Date=d.Date;
    sTime.WeekDay=d.WeekDay;sTime.Hours=t.Hours;
    sTime.Minutes=t.Minutes;sTime.Seconds=t.Seconds;
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
    if(HAL_RTC_SetDate(&hrtc,&d,RTC_FORMAT_BIN)==HAL_OK)
        (void)HAL_RTC_SetTime(&hrtc,&t,RTC_FORMAT_BIN);
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
    AddItem("-Time",UI_ITEM_PARENTS,img_home,&s_time_item,parent,&s_time_page,0);
    AddPage("[Time]",&s_time_page,UI_PAGE_TEXT,parent);
    AddItem("[Back]",UI_ITEM_RETURN,0,&s_back,&s_time_page,parent,0);
    for(i=0u;i<6u;i++)
        AddItem(labels[i],UI_ITEM_DATA,0,&s_fields[i],&s_time_page,0,0);
}
