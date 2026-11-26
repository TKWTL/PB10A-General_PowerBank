/* PB10A system service adapted from IH01A/framework/system.c.
 * Hardware: TIM3_CH4/PB1 buzzer; Cortex-M0+ NVIC reset, no heater hook.
 */
#include "system_algo.h"
#include "beep_service.h"
#include "power_algo.h"
#include "main.h"
#include "core/ui.h"
#include "i18n.h"
static ui_item_t s_click_item,s_volume_item,s_play_music_item,s_reset_item;
static ui_item_t s_language_item;
static ui_data_t s_language_data;
static ui_element_t s_language_element;
static uint8_t s_language_on;
static ui_item_t s_vdda_item,s_temp_item;
static ui_data_t s_vdda_data,s_temp_data;
static ui_element_t s_vdda_element,s_temp_element;
static int s_vdda_cv, s_die_temp_c10;
static ui_data_t s_click_data,s_volume_data;
static ui_element_t s_click_element,s_volume_element;
static uint8_t s_click_on;
enum { NAV_RADIO,
       NAV_USB,
       NAV_AUX,
       NAV_COUNT };
static uint8_t s_nav_enabled[NAV_COUNT] = {1u, 1u, 1u};
static ui_item_t s_nav_items[NAV_COUNT];
static ui_data_t s_nav_data[NAV_COUNT];
static ui_element_t s_nav_elements[NAV_COUNT];
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
static void system_language_changed(ui_t *ui) {
    (void)ui;
    I18N_SetLanguage(s_language_on ? I18N_LANG_ZH_CN : I18N_LANG_EN);
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
    s_volume_data.name = " Beep Volume";
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
    s_language_on = I18N_GetLanguage() == I18N_LANG_ZH_CN;
    s_language_data.name = " Language";
    s_language_data.ptr = &s_language_on;
    s_language_data.function = system_language_changed;
    s_language_data.dataType = UI_DATA_SWITCH;
    s_language_data.actionType = UI_DATA_ACTION_RW;
    s_language_element.data = &s_language_data;
    Create_element(&s_language_item, &s_language_element);
    AddItem(" Language", UI_ITEM_DATA, 0, &s_language_item, page, 0, 0);
    AddItem("!System Reset", UI_ITEM_ONCE_FUNCTION, 0, &s_reset_item, page, 0, system_reset_selected);
}

void System_AddNavItems(ui_page_t *page) {
    static const char *const names[NAV_COUNT] = {" Headphone Nav", " USB Port Nav", " Aux-A Nav"};
    for (unsigned i = 0u; i < NAV_COUNT; ++i) {
        s_nav_data[i].name = names[i];
        s_nav_data[i].ptr = &s_nav_enabled[i];
        s_nav_data[i].dataType = UI_DATA_SWITCH;
        s_nav_data[i].actionType = UI_DATA_ACTION_RW;
        s_nav_elements[i].data = &s_nav_data[i];
        Create_element(&s_nav_items[i], &s_nav_elements[i]);
        AddItem(names[i], UI_ITEM_DATA, 0, &s_nav_items[i], page, 0, 0);
    }
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

uint8_t System_AutoRadioNavigationEnabled(void) { return s_nav_enabled[NAV_RADIO]; }
uint8_t System_AutoPowerbankNavigationEnabled(void) { return s_nav_enabled[NAV_USB]; }

uint8_t System_AutoAuxANavigationEnabled(void) { return s_nav_enabled[NAV_AUX]; }
