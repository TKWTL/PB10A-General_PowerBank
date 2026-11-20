/* PB10A system service adapted from IH01A/framework/system.c.
 * Hardware: TIM3_CH4/PB1 buzzer; Cortex-M0+ NVIC reset, no heater hook.
 */
#include "system_algo.h"
#include "beep_algo.h"
#include "power_algo.h"
#include "main.h"
#include "core/ui.h"
static ui_item_t s_click_item,s_mute_item,s_reset_item;
static ui_item_t s_radio_auto_item,s_power_auto_item;
static ui_item_t s_vdda_item,s_temp_item;
static ui_data_t s_vdda_data,s_temp_data;
static ui_element_t s_vdda_element,s_temp_element;
static int s_vdda_mv,s_die_temp_c10;
static ui_data_t s_click_data,s_mute_data;
static ui_data_t s_radio_auto_data,s_power_auto_data;
static ui_element_t s_click_element,s_mute_element;
static ui_element_t s_radio_auto_element,s_power_auto_element;
static uint8_t s_click_on,s_muted,s_radio_auto,s_power_auto;
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
    s_radio_auto_data.name=" Headphone Auto UI";
    s_radio_auto_data.ptr=&s_radio_auto;
    s_radio_auto_data.dataType=UI_DATA_SWITCH;
    s_radio_auto_data.actionType=UI_DATA_ACTION_RW;
    s_radio_auto_element.data=&s_radio_auto_data;
    Create_element(&s_radio_auto_item,&s_radio_auto_element);
    AddItem(" Headphone Auto UI",UI_ITEM_DATA,0,&s_radio_auto_item,page,0,0);

    s_power_auto_data.name=" Port Auto UI";
    s_power_auto_data.ptr=&s_power_auto;
    s_power_auto_data.dataType=UI_DATA_SWITCH;
    s_power_auto_data.actionType=UI_DATA_ACTION_RW;
    s_power_auto_element.data=&s_power_auto_data;
    Create_element(&s_power_auto_item,&s_power_auto_element);
    AddItem(" Port Auto UI",UI_ITEM_DATA,0,&s_power_auto_item,page,0,0);

    AddItem(" System Reset",UI_ITEM_ONCE_FUNCTION,0,&s_reset_item,page,0,system_reset_selected);
}

void System_AddStatisticsItems(ui_page_t *page) {
    if(!page)return;
    s_vdda_data.name=" VDDA mV";s_vdda_data.ptr=&s_vdda_mv;
    s_vdda_data.dataType=UI_DATA_INT;s_vdda_data.actionType=UI_DATA_ACTION_RO;
    s_vdda_element.data=&s_vdda_data;
    Create_element(&s_vdda_item,&s_vdda_element);
    AddItem(" VDDA mV",UI_ITEM_DATA,0,&s_vdda_item,page,0,0);
    s_temp_data.name=" Die Temp C";s_temp_data.ptr=&s_die_temp_c10;
    s_temp_data.dataType=UI_DATA_FLOAT;s_temp_data.actionType=UI_DATA_ACTION_RO;
    s_temp_data.decimals=1u;
    s_temp_element.data=&s_temp_data;
    Create_element(&s_temp_item,&s_temp_element);
    AddItem(" Die Temp C",UI_ITEM_DATA,0,&s_temp_item,page,0,0);
}

void System_Poll(void) {
    PowerAlgo_GetSystemAdc(&s_vdda_mv,&s_die_temp_c10,0);
}

uint8_t System_AutoRadioNavigationEnabled(void){return s_radio_auto;}
uint8_t System_AutoPowerbankNavigationEnabled(void){return s_power_auto;}
