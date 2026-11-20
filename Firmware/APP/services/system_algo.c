/* PB10A system service adapted from IH01A/framework/system.c.
 * Hardware: TIM3_CH4/PB1 buzzer; Cortex-M0+ NVIC reset, no heater hook.
 */
#include "system_algo.h"
#include "beep_algo.h"
#include "time_algo.h"
#include "millis.h"
#include "main.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include <stdio.h>
static ui_item_t s_click_item,s_mute_item,s_reset_item,s_uptime_item;
static ui_data_t s_click_data,s_mute_data;
static ui_element_t s_click_element,s_mute_element;
static uint8_t s_click_on,s_muted;
static char s_uptime_text[32]="Uptime: --";
static uint32_t s_last_seconds=0xFFFFFFFFu,s_last_poll;
static uint8_t s_stats_ready;
static void system_click_changed(ui_t *ui)
{ (void)ui;beep_set_key_enabled(s_click_on); }
static void system_mute_changed(ui_t *ui)
{ (void)ui;beep_set_muted(s_muted); }
void System_Reset(void) {
    beep_stop();
    NVIC_SystemReset();
}

static void system_reset_selected(ui_t *ui)
{ (void)ui;System_Reset(); }
void System_AddSettingsItems(ui_page_t *page) {
    if(!page)return;
    s_click_on=beep_get_key_enabled();
    s_click_data.name=" Click Beep";
    s_click_data.ptr=&s_click_on;
    s_click_data.function=system_click_changed;
    s_click_data.functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    s_click_data.dataType=UI_DATA_SWITCH;
    s_click_data.actionType=UI_DATA_ACTION_RW;
    s_click_element.data=&s_click_data;
    Create_element(&s_click_item,&s_click_element);
    AddItem(" Click Beep",UI_ITEM_DATA,0,&s_click_item,page,0,0);
    s_muted=beep_is_muted();
    s_mute_data.name=" Mute Beep";
    s_mute_data.ptr=&s_muted;
    s_mute_data.function=system_mute_changed;
    s_mute_data.functionType=UI_DATA_FUNCTION_STEP_EXECUTE;
    s_mute_data.dataType=UI_DATA_SWITCH;
    s_mute_data.actionType=UI_DATA_ACTION_RW;
    s_mute_element.data=&s_mute_data;
    Create_element(&s_mute_item,&s_mute_element);
    AddItem(" Mute Beep",UI_ITEM_DATA,0,&s_mute_item,page,0,0);
    AddItem(" System Reset",UI_ITEM_ONCE_FUNCTION,0,&s_reset_item,page,0,system_reset_selected);
}

static void system_uptime_page(ui_t *ui) {
    uint8_t fg;
    if(!ui||ui->action!=UI_ACTION_NONE)return;
    Disp_ClearBuffer();
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(0,13,s_uptime_text);
    Disp_SendBuffer();
}

void System_AddStatisticsItems(ui_page_t *page) {
    if(!page)return;
    AddItem(" Uptime",UI_ITEM_WORD,0,&s_uptime_item,page,0,system_uptime_page);
    s_stats_ready=1u;s_last_poll=Millis()-100u;
}

void System_Poll(void) {
    uint32_t seconds,now=Millis();
    char value[24];
    if(!s_stats_ready||(uint32_t)(now-s_last_poll)<100u)return;
    s_last_poll=now;
    if(!Time_GetUptimeSeconds(&seconds))
        { (void)snprintf(s_uptime_text,sizeof(s_uptime_text),"Uptime: RTC error");return; }
    if(seconds==s_last_seconds)return;
    s_last_seconds=seconds;
    Time_FormatDuration(seconds,value,sizeof(value));
    (void)snprintf(s_uptime_text,sizeof(s_uptime_text),"Uptime: %s",value);
}
