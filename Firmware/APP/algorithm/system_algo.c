/* PB10A system service adapted from IH01A/framework/system.c.
 * Hardware: TIM3_CH4/PB1 buzzer; Cortex-M0+ NVIC reset, no heater hook.
 */
#include "system_algo.h"
#include "beep_service.h"
#include "power_algo.h"
#include "main.h"
#include "core/ui.h"
static ui_item_t s_click_item,s_volume_item,s_play_music_item,s_reset_item;
static ui_item_t s_radio_auto_item,s_power_auto_item,s_aux_auto_item;
static ui_item_t s_vdda_item,s_temp_item;
static ui_data_t s_vdda_data,s_temp_data;
static ui_element_t s_vdda_element,s_temp_element;
static int s_vdda_cv, s_die_temp_c10;
static ui_data_t s_click_data,s_volume_data;
static ui_data_t s_radio_auto_data,s_power_auto_data,s_aux_auto_data;
static ui_element_t s_click_element,s_volume_element;
static ui_element_t s_radio_auto_element,s_power_auto_element,s_aux_auto_element;
static uint8_t s_click_on,s_radio_auto,s_power_auto,s_aux_auto;
static int s_volume_level;
/* Determination (Undertale), right hand, first eight measures.
 * F# major, 6/8, dotted-quarter = 115 BPM, eighth note ~= 174 ms.
 * m2/m6 first note is #4 (B# = C), middle octave: BEEP_C5.
 */
static const uint16_t s_determination_rh[] = {
    /* m1 */
    BEEP_FS5, 174u, BEEP_F5, 174u, BEEP_DS5, 174u,
    BEEP_CS5, 174u, BEEP_DS5, 174u, BEEP_AS4, 174u,
    /* m2 */
    BEEP_C5, 174u, BEEP_REST, 174u, BEEP_GS4, 174u,
    BEEP_REST, 174u, BEEP_DS5, 174u, BEEP_F5, 174u,
    /* m3 */
    BEEP_FS5, 174u, BEEP_REST, 174u, BEEP_GS5, 174u,
    BEEP_REST, 174u, BEEP_CS6, 174u, BEEP_REST, 174u,
    /* m4 */
    BEEP_AS5, 696u, BEEP_REST, 348u,
    /* m5 */
    BEEP_FS5, 174u, BEEP_F5, 174u, BEEP_DS5, 174u,
    BEEP_CS5, 174u, BEEP_DS5, 174u, BEEP_AS4, 174u,
    /* m6 */
    BEEP_C5, 174u, BEEP_REST, 174u, BEEP_GS4, 174u,
    BEEP_REST, 174u, BEEP_DS4, 174u, BEEP_F4, 174u,
    /* m7 */
    BEEP_FS4, 174u, BEEP_REST, 174u, BEEP_F4, 174u,
    BEEP_REST, 174u, BEEP_CS4, 174u, BEEP_REST, 174u,
    /* m8 */
    BEEP_DS4, 696u, BEEP_REST, 348u,
    BEEP_END
};

static void system_play_music(ui_t *ui) {
    (void)ui;
    beep_play(s_determination_rh);
}

static void system_click_changed(ui_t *ui)
{ (void)ui;beep_set_key_enabled(s_click_on); }
static void system_volume_changed(ui_t *ui) {
    (void)ui;
    beep_set_volume((uint8_t)s_volume_level);
}
void System_Reset(void) {
    beep_stop();
    NVIC_SystemReset();
}

static void system_reset_selected(ui_t *ui)
{ (void)ui;System_Reset(); }
__attribute__((noinline)) void System_AddSettingsItems(ui_page_t *page) {
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
    s_volume_level = (int)beep_get_volume();
    s_volume_data.name = "Beep Volume";
    s_volume_data.ptr = &s_volume_level;
    s_volume_data.function = system_volume_changed;
    s_volume_data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    s_volume_data.dataType = UI_DATA_INT;
    s_volume_data.actionType = UI_DATA_ACTION_RW;
    s_volume_data.min = 0;
    s_volume_data.max = BEEP_VOLUME_MAX;
    s_volume_data.step = 1;
    s_volume_element.data = &s_volume_data;
    Create_element(&s_volume_item, &s_volume_element);
    AddItem(" Beep Volume", UI_ITEM_DATA, 0, &s_volume_item, page, 0, 0);
    AddItem("!Play Music", UI_ITEM_ONCE_FUNCTION, 0, &s_play_music_item, page, 0, system_play_music);
    s_radio_auto_data.name = " Headphone Nav";
    s_radio_auto_data.ptr=&s_radio_auto;
    s_radio_auto_data.dataType=UI_DATA_SWITCH;
    s_radio_auto_data.actionType=UI_DATA_ACTION_RW;
    s_radio_auto_element.data=&s_radio_auto_data;
    Create_element(&s_radio_auto_item,&s_radio_auto_element);
    AddItem(" Headphone Nav", UI_ITEM_DATA, 0, &s_radio_auto_item, page, 0, 0);

    s_power_auto_data.name = " USB Port Nav";
    s_power_auto_data.ptr=&s_power_auto;
    s_power_auto_data.dataType=UI_DATA_SWITCH;
    s_power_auto_data.actionType=UI_DATA_ACTION_RW;
    s_power_auto_element.data=&s_power_auto_data;
    Create_element(&s_power_auto_item,&s_power_auto_element);
    AddItem(" USB Port Nav", UI_ITEM_DATA, 0, &s_power_auto_item, page, 0, 0);

    s_aux_auto_data.name = " Aux-A Nav";
    s_aux_auto_data.ptr=&s_aux_auto;
    s_aux_auto_data.dataType=UI_DATA_SWITCH;
    s_aux_auto_data.actionType=UI_DATA_ACTION_RW;
    s_aux_auto_element.data=&s_aux_auto_data;
    Create_element(&s_aux_auto_item,&s_aux_auto_element);
    AddItem(" Aux-A Nav", UI_ITEM_DATA, 0, &s_aux_auto_item, page, 0, 0);

    AddItem("!System Reset", UI_ITEM_ONCE_FUNCTION, 0, &s_reset_item, page, 0, system_reset_selected);
}

void System_AddStatisticsItems(ui_page_t *page) {
    if(!page)return;
    s_vdda_data.name = " MCU Supply V";
    s_vdda_data.ptr = &s_vdda_cv;
    s_vdda_data.dataType = UI_DATA_FLOAT;
    s_vdda_data.decimals = 2u;
    s_vdda_data.actionType = UI_DATA_ACTION_RO;
    s_vdda_element.data=&s_vdda_data;
    Create_element(&s_vdda_item,&s_vdda_element);
    AddItem(" MCU Supply V", UI_ITEM_DATA, 0, &s_vdda_item, page, 0, 0);
    s_temp_data.name = " MCU Temp°C";
    s_temp_data.ptr = &s_die_temp_c10;
    s_temp_data.dataType=UI_DATA_FLOAT;s_temp_data.actionType=UI_DATA_ACTION_RO;
    s_temp_data.decimals=1u;
    s_temp_element.data=&s_temp_data;
    Create_element(&s_temp_item,&s_temp_element);
    AddItem(" MCU Temp°C", UI_ITEM_DATA, 0, &s_temp_item, page, 0, 0);
}

void System_Poll(void) {
    int vdda_mv;
    PowerAlgo_GetSystemAdc(&vdda_mv, &s_die_temp_c10, 0);
    s_vdda_cv = (vdda_mv + 5) / 10;
}

uint8_t System_AutoRadioNavigationEnabled(void){return s_radio_auto;}
uint8_t System_AutoPowerbankNavigationEnabled(void){return s_power_auto;}

uint8_t System_AutoAuxANavigationEnabled(void){return s_aux_auto;}
