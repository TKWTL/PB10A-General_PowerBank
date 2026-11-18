/* PB10A top-level navigation. Seven icon items; submenus by function. */
#include "ui_conf.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "images/image.h"
#include "time_service.h"
#include "i2c_scanner.h"
#include "powerbank_app.h"
#include "radio_app.h"
#include "system_algo.h"
#include "power_adc_algo.h"
static ui_page_t s_home,s_settings,s_stats,s_game,s_tools,s_display;
static ui_page_t s_sys,s_stat_power,s_stat_battery,s_stat_sensors;
static ui_item_t s_powerbank,s_radio,s_settings_item,s_stats_item,s_game_item,s_tools_item,s_about;
static ui_item_t s_settings_back,s_stats_back,s_game_back,s_tools_back;
static ui_item_t s_display_item,s_system_item,s_display_back,s_system_back;
static ui_item_t s_stat_power_item,s_stat_battery_item,s_stat_sensors_item;
static ui_item_t s_stat_power_back,s_stat_battery_back,s_stat_sensors_back;
static ui_item_t s_dino,s_scanner,s_tools_info;
static ui_text_t s_about_text,s_dino_text,s_tools_text;
static ui_element_t s_about_el,s_dino_el,s_tools_el;
static void add_return(ui_page_t *page,ui_page_t *parent,ui_item_t *item,uint8_t icon)
{
    (void)icon;
    AddItem("[Back]",UI_ITEM_RETURN,page->type==UI_PAGE_ICON?img_home:0,item,page,parent,0);
}
void Create_Parameter(ui_t *ui)
{
    Create_Disp_Parameters(ui);
    Create_TimeSet_Parameters(ui);
}
void Create_Text(ui_t *ui)
{
    (void)ui;
    s_about_text.ptr="PB10A General Powerbank\nSTM32G070 / MiaoUI\nTKWTL open firmware";
    s_about_text.font=UI_FONT;s_about_text.fontHight=UI_FONT_HIGHT;
    s_about_text.fontWidth=UI_FONT_WIDTH;
    s_about_el.text=&s_about_text;Create_element(&s_about,&s_about_el);
    s_dino_text.ptr="Game framework ready\nNo game installed";
    s_dino_text.font=UI_FONT;s_dino_text.fontHight=UI_FONT_HIGHT;
    s_dino_text.fontWidth=UI_FONT_WIDTH;
    s_dino_el.text=&s_dino_text;Create_element(&s_dino,&s_dino_el);
    s_tools_text.ptr="PB10A diagnostics\nInstall more apps in\nAPP/functions";
    s_tools_text.font=UI_FONT;s_tools_text.fontHight=UI_FONT_HIGHT;
    s_tools_text.fontWidth=UI_FONT_WIDTH;
    s_tools_el.text=&s_tools_text;Create_element(&s_tools_info,&s_tools_el);
}
void Create_MenuTree(ui_t *ui)
{
    (void)ui;
    AddPage("[PB10A]",&s_home,UI_PAGE_ICON,0);
    AddItem("-Powerbank",UI_ITEM_WORD,img_powerbank,&s_powerbank,&s_home,0,Powerbank_AppRun);
    AddItem("-Radio",UI_ITEM_WORD,img_radio,&s_radio,&s_home,0,Radio_AppRun);
    AddItem("-Settings",UI_ITEM_PARENTS,img_configuration,&s_settings_item,&s_home,&s_settings,0);
    AddItem("-Statistics",UI_ITEM_PARENTS,img_statistics,&s_stats_item,&s_home,&s_stats,0);
    AddItem("-Game",UI_ITEM_PARENTS,img_games,&s_game_item,&s_home,&s_game,0);
    AddItem("-Tools",UI_ITEM_PARENTS,img_tools,&s_tools_item,&s_home,&s_tools,0);
    AddItem("-About",UI_ITEM_WORD,img_user_account,&s_about,&s_home,0,0);

    AddPage("[Settings]",&s_settings,UI_PAGE_TEXT,&s_home);
    add_return(&s_settings,&s_home,&s_settings_back,0);
    AddItem(" Display",UI_ITEM_PARENTS,0,&s_display_item,&s_settings,&s_display,0);
    Add_TimeSet_Items(&s_settings); /* Time submenu and its editor from service */
    AddItem(" System",UI_ITEM_PARENTS,0,&s_system_item,&s_settings,&s_sys,0);

    AddPage("[Display]",&s_display,UI_PAGE_TEXT,&s_settings);
    add_return(&s_display,&s_settings,&s_display_back,0);
    Add_Disp_Items(&s_display);
    AddPage("[System]",&s_sys,UI_PAGE_TEXT,&s_settings);
    add_return(&s_sys,&s_settings,&s_system_back,0);
    System_AddSettingsItems(&s_sys);

    AddPage("[Statistics]",&s_stats,UI_PAGE_TEXT,&s_home);
    add_return(&s_stats,&s_home,&s_stats_back,0);
    AddItem(" Power",UI_ITEM_PARENTS,0,&s_stat_power_item,&s_stats,&s_stat_power,0);
    AddItem(" Battery",UI_ITEM_PARENTS,0,&s_stat_battery_item,&s_stats,&s_stat_battery,0);
    AddItem(" Sensors",UI_ITEM_PARENTS,0,&s_stat_sensors_item,&s_stats,&s_stat_sensors,0);
    AddPage("[Power]",&s_stat_power,UI_PAGE_TEXT,&s_stats);
    add_return(&s_stat_power,&s_stats,&s_stat_power_back,0);
    PowerADC_AddStatisticsItems(&s_stat_power);
    System_AddStatisticsItems(&s_stat_power);
    AddPage("[Battery]",&s_stat_battery,UI_PAGE_TEXT,&s_stats);
    add_return(&s_stat_battery,&s_stats,&s_stat_battery_back,0);
    AddPage("[Sensors]",&s_stat_sensors,UI_PAGE_TEXT,&s_stats);
    add_return(&s_stat_sensors,&s_stats,&s_stat_sensors_back,0);

    AddPage("[Game]",&s_game,UI_PAGE_ICON,&s_home);
    add_return(&s_game,&s_home,&s_game_back,0);
    AddItem("-Dinosaur",UI_ITEM_WORD,img_dinosuar,&s_dino,&s_game,0,0);
    AddPage("[Tools]",&s_tools,UI_PAGE_ICON,&s_home);
    add_return(&s_tools,&s_home,&s_tools_back,0);
    AddItem("-I2C Scanner",UI_ITEM_WORD,img_scan,&s_scanner,&s_tools,0,i2c_Scanner);
    AddItem("-Diagnostics",UI_ITEM_WORD,img_burn_in,&s_tools_info,&s_tools,0,0);
}
void MiaoUi_Setup(ui_t *ui)
{
    Create_UI(ui,&s_powerbank);
}
