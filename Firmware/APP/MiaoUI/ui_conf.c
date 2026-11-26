/* PB10A top-level navigation. TEXT item labels:
 *   [Back]    Return entries use square brackets; 'B' at x=one glyph.
 *    Option   Data items start with one blank to align with 'B'.
 *   !Action   Executable function items start with '!'.
 *   -Submenu  Parent entries start with '-'.
 * ICON page labels keep their native conventions.
 */
#include "ui_conf.h"
#include "miaoui.h"
#include "time_service.h"
#include "apps.h"
#include "games/arkanoid/arkanoid_assets.h"
#include "games/pacman/pacman_assets.h"
#include "games/bomber/bomber_assets.h"
#include "games/invaders/invaders_assets.h"
#include "games/tetris/tetris_assets.h"
#include "algorithms.h"
#include "pm_api.h"
#include "main.h"
#include "millis.h"
static ui_page_t s_home,s_settings,s_stats,s_game,s_tools,s_display;
static ui_page_t s_sys, s_sleep_wake, s_stat_power, s_stat_battery, s_stat_sensors, s_stat_fault, s_stat_system;
static ui_item_t s_powerbank,s_radio,s_settings_item,s_stats_item,s_game_item,s_tools_item,s_about;
static ui_item_t s_settings_back,s_stats_back,s_game_back,s_tools_back;
static ui_page_t s_power_limit, s_ports, s_protocol1, s_protocol2;
static ui_item_t s_power_limit_item, s_ports_item, s_protocol1_item, s_protocol2_item;
static ui_item_t s_power_limit_back, s_ports_back, s_protocol1_back, s_protocol2_back;
static ui_item_t s_display_item,s_sleep_wake_item,s_system_item,s_display_back,s_sleep_wake_back,s_system_back;
static ui_item_t s_stat_power_item, s_stat_battery_item, s_stat_sensors_item, s_stat_fault_item, s_stat_system_item;
static ui_item_t s_stat_power_back, s_stat_battery_back, s_stat_sensors_back, s_stat_fault_back, s_stat_system_back;
static ui_item_t s_arkanoid, s_tetris, s_pacman, s_bomber, s_invaders, s_scanner, s_leveler, s_accel_cal, s_sos, s_compass, s_altimeter, s_compass_cal;
static ui_item_t s_sleep_options_item;
static ui_data_t s_sleep_data;
static ui_element_t s_sleep_element;
static int s_sleep_selected = 3; /* 1 min, matching the PM default. */
static const char *const s_sleep_labels[] = {
    "Off", "15s", "30s", "1min", "2min", "5min", "10min", "15min", "30min"};
static const int s_sleep_seconds[] = {0, 15, 30, 60, 120, 300, 600, 900, 1800};
static ui_text_t s_about_text;
static ui_element_t s_about_el;
/* Only one pending target; Radio (1) outranks Powerbank (2). */
static uint8_t s_nav_pending,s_nav_active;
static uint32_t s_power_nav_requested_at;
static uint8_t s_mic_candidate, s_mic_latched;
static uint32_t s_mic_edge_at;

static void add_return(ui_page_t *page,ui_page_t *parent,ui_item_t *item,uint8_t icon) {
    (void)icon;
    AddItem("[Back]",UI_ITEM_RETURN,page->type==UI_PAGE_ICON?img_home:0,item,page,parent,0);
}

/* RAM-only enum, reusing the normal parameter editor. */
static void sleep_option_changed(ui_t *ui) {
    (void)ui;
    pm_api_set_sleep_timeout(s_sleep_seconds[s_sleep_selected]);
}

static void add_sleep_options(ui_page_t *parent) {
    s_sleep_data.name = "Sleep";
    s_sleep_data.ptr = &s_sleep_selected;
    s_sleep_data.function = sleep_option_changed;
    s_sleep_data.functionType = UI_DATA_FUNCTION_STEP_EXECUTE;
    s_sleep_data.dataType = UI_DATA_ENUM;
    s_sleep_data.actionType = UI_DATA_ACTION_RW;
    s_sleep_data.min = 0;
    s_sleep_data.max = (int)(sizeof(s_sleep_labels) / sizeof(s_sleep_labels[0])) - 1;
    s_sleep_data.step = 1;
    s_sleep_data.options = s_sleep_labels;
    s_sleep_element.data = &s_sleep_data;
    Create_element(&s_sleep_options_item, &s_sleep_element);
    AddItem(" Sleep Options", UI_ITEM_DATA, 0, &s_sleep_options_item, parent, 0, 0);
}

void Create_Parameter(ui_t *ui) {
    Create_Disp_Parameters(ui);
    Create_TimeSet_Parameters(ui);
}

void Create_Text(ui_t *ui) {
    (void)ui;
    s_about_text.ptr = "  PB10A Power Bank\n    PD/PPS/UFCS\nhttps://github.com/TKWTL/PB10A-General_PowerBank";
    s_about_text.font=UI_FONT;s_about_text.fontHight=UI_FONT_HIGHT;
    s_about_text.fontWidth=UI_FONT_WIDTH;
    s_about_el.text = &s_about_text;
    Create_element(&s_about, &s_about_el);
}

/* Menu hierarchy is deliberately visible through indentation. */
// clang-format off
void Create_MenuTree(ui_t *ui) {
    (void)ui;
    AddPage("[PB10A]",&s_home,UI_PAGE_ICON,0);
        AddItem("-Powerbank",UI_ITEM_WORD,img_power,&s_powerbank,&s_home,0,Powerbank_AppRun);
        UI_SetLeaveHook(&s_powerbank,Powerbank_AppOnLeave);
        AddItem("-Radio",UI_ITEM_WORD,img_radio,&s_radio,&s_home,0,Radio_AppRun);
        UI_SetLeaveHook(&s_radio,Radio_AppOnLeave);
        AddItem("-Game",UI_ITEM_PARENTS,img_games,&s_game_item,&s_home,&s_game,0);
        AddItem("-Tools",UI_ITEM_PARENTS,img_tools,&s_tools_item,&s_home,&s_tools,0);
        AddItem("-Settings",UI_ITEM_PARENTS,img_configuration,&s_settings_item,&s_home,&s_settings,0);
        AddItem("-Statistics",UI_ITEM_PARENTS,img_statistics,&s_stats_item,&s_home,&s_stats,0);

    AddPage("[Game]",&s_game,UI_PAGE_ICON,&s_home);
        add_return(&s_game,&s_home,&s_game_back,0);
        AddItem("-Tetris",UI_ITEM_WORD,img_tetris,&s_tetris,&s_game,0,Game_TetrisRun);
        UI_SetLeaveHook(&s_tetris,Game_TetrisOnLeave);
        AddItem("-Pac-Man",UI_ITEM_WORD,img_pacman,&s_pacman,&s_game,0,Game_PacmanRun);
        UI_SetLeaveHook(&s_pacman,Game_PacmanOnLeave);
        AddItem("-Bomber",UI_ITEM_WORD,img_bomber,&s_bomber,&s_game,0,Game_BomberRun);
        UI_SetLeaveHook(&s_bomber,Game_BomberOnLeave);
        AddItem("-Invaders",UI_ITEM_WORD,img_invaders,&s_invaders,&s_game,0,Game_InvadersRun);
        UI_SetLeaveHook(&s_invaders,Game_InvadersOnLeave);
        AddItem("-Arkanoid",UI_ITEM_WORD,img_arkanoid,&s_arkanoid,&s_game,0,Game_ArkanoidRun);
        UI_SetLeaveHook(&s_arkanoid,Game_ArkanoidOnLeave);

    AddPage("[Tools]",&s_tools,UI_PAGE_ICON,&s_home);
        add_return(&s_tools,&s_home,&s_tools_back,0);
        AddItem("-Leveler",UI_ITEM_WORD,img_leveler,
            &s_leveler,&s_tools,0,Motion_LevelerRun);
        UI_SetLeaveHook(&s_leveler,Motion_LevelerOnLeave);
        AddItem("-Compass",UI_ITEM_WORD,img_compass,&s_compass,&s_tools,0,Compass_AppRun);
        UI_SetLeaveHook(&s_compass,Compass_AppOnLeave);
        AddItem("-Altimeter",UI_ITEM_WORD,img_altimeter,&s_altimeter,&s_tools,0,Altimeter_AppRun);
        UI_SetLeaveHook(&s_altimeter,Altimeter_AppOnLeave);
        AddItem("-SOS Blink",UI_ITEM_WORD,img_sos,&s_sos,&s_tools,0,SOS_AppRun);
        UI_SetLeaveHook(&s_sos,SOS_AppOnLeave);
        AddItem("-Accel Calibration",UI_ITEM_WORD,img_leveler_cal,
            &s_accel_cal,&s_tools,0,Motion_CalibrationRun);
        UI_SetLeaveHook(&s_accel_cal,Motion_CalibrationOnLeave);
        AddItem("-Mag Calibration",UI_ITEM_WORD,img_compass_cal,&s_compass_cal,&s_tools,0,CompassCal_AppRun);
        UI_SetLeaveHook(&s_compass_cal,CompassCal_AppOnLeave);
        AddItem("-I2C Scanner",UI_ITEM_WORD,img_scan,&s_scanner,&s_tools,0,i2c_Scanner);

    AddPage("[Settings]",&s_settings,UI_PAGE_TEXT,&s_home);
        add_return(&s_settings,&s_home,&s_settings_back,0);
        AddItem("-Ports",UI_ITEM_PARENTS,0,&s_ports_item,&s_settings,&s_ports,0);
        AddItem("-Protocol#1",UI_ITEM_PARENTS,0,&s_protocol1_item,&s_settings,&s_protocol1,0);
        AddItem("-Protocol#2",UI_ITEM_PARENTS,0,&s_protocol2_item,&s_settings,&s_protocol2,0);
        AddItem("-Power Limits",UI_ITEM_PARENTS,0,&s_power_limit_item,&s_settings,&s_power_limit,0);
        AddItem("-Display",UI_ITEM_PARENTS,0,&s_display_item,&s_settings,&s_display,0);
        AddItem("-Sleep & Wake",UI_ITEM_PARENTS,0,&s_sleep_wake_item,&s_settings,&s_sleep_wake,0);
        Add_TimeSet_Items(&s_settings);
        AddItem("-System",UI_ITEM_PARENTS,0,&s_system_item,&s_settings,&s_sys,0);

        AddPage("[Ports]",&s_ports,UI_PAGE_TEXT,&s_settings);
            add_return(&s_ports,&s_settings,&s_ports_back,0);
            PowerAlgo_AddPortItems(&s_ports);

        AddPage("[Protocol#1]",&s_protocol1,UI_PAGE_TEXT,&s_settings);
            add_return(&s_protocol1,&s_settings,&s_protocol1_back,0);
            PowerAlgo_AddProtocolItems(&s_protocol1,1u);

        AddPage("[Protocol#2]",&s_protocol2,UI_PAGE_TEXT,&s_settings);
            add_return(&s_protocol2,&s_settings,&s_protocol2_back,0);
            PowerAlgo_AddProtocolItems(&s_protocol2,2u);

        AddPage("[Power Limits]",&s_power_limit,UI_PAGE_TEXT,&s_settings);
            add_return(&s_power_limit,&s_settings,&s_power_limit_back,0);
            PowerAlgo_AddPowerLimitItems(&s_power_limit);

        AddPage("[Display]",&s_display,UI_PAGE_TEXT,&s_settings);
            add_return(&s_display,&s_settings,&s_display_back,0);
            Add_Disp_Items(&s_display);

        AddPage("[Sleep & Wake]",&s_sleep_wake,UI_PAGE_TEXT,&s_settings);
            add_return(&s_sleep_wake,&s_settings,&s_sleep_wake_back,0);
            add_sleep_options(&s_sleep_wake);
            SC7A20Algo_AddWakeSettingsItems(&s_sleep_wake);
            RadioAlgo_AddSleepWakeItems(&s_sleep_wake);

        AddPage("[System]",&s_sys,UI_PAGE_TEXT,&s_settings);
            add_return(&s_sys,&s_settings,&s_system_back,0);
            System_AddSettingsItems(&s_sys);

    AddPage("[Statistics]",&s_stats,UI_PAGE_TEXT,&s_home);
        add_return(&s_stats,&s_home,&s_stats_back,0);
        AddItem("-Power",UI_ITEM_PARENTS,0,&s_stat_power_item,&s_stats,&s_stat_power,0);
        AddItem("-Battery",UI_ITEM_PARENTS,0,&s_stat_battery_item,&s_stats,&s_stat_battery,0);
        AddItem("-Sensors",UI_ITEM_PARENTS,0,&s_stat_sensors_item,&s_stats,&s_stat_sensors,0);
        AddItem("-Fault",UI_ITEM_PARENTS,0,&s_stat_fault_item,&s_stats,&s_stat_fault,0);
        AddItem("-System",UI_ITEM_PARENTS,0,&s_stat_system_item,&s_stats,&s_stat_system,0);
        AddItem("-About", UI_ITEM_WORD, 0, &s_about, &s_stats, 0, 0);

        AddPage("[Power]",&s_stat_power,UI_PAGE_TEXT,&s_stats);
            add_return(&s_stat_power,&s_stats,&s_stat_power_back,0);
            PowerAlgo_AddStatisticsItems(&s_stat_power);

        AddPage("[Battery]",&s_stat_battery,UI_PAGE_TEXT,&s_stats);
            add_return(&s_stat_battery,&s_stats,&s_stat_battery_back,0);
            PowerAlgo_AddBatteryItems(&s_stat_battery);

        AddPage("[Sensors]",&s_stat_sensors,UI_PAGE_TEXT,&s_stats);
            add_return(&s_stat_sensors,&s_stats,&s_stat_sensors_back,0);
            SC7A20Algo_AddStatisticsItems(&s_stat_sensors);
            MMC5603Algo_AddStatisticsItems(&s_stat_sensors);
            BMP180Algo_AddStatisticsItems(&s_stat_sensors);

        AddPage("[Fault]",&s_stat_fault,UI_PAGE_TEXT,&s_stats);
            add_return(&s_stat_fault,&s_stats,&s_stat_fault_back,0);
            PowerAlgo_AddFaultItems(&s_stat_fault);

        AddPage("[System]",&s_stat_system,UI_PAGE_TEXT,&s_stats);
            add_return(&s_stat_system,&s_stats,&s_stat_system_back,0);
            System_AddStatisticsItems(&s_stat_system);
}
// clang-format on

ui_rotation_policy_t MiaoUi_ItemRotationPolicy(const ui_item_t *item) {
    if (item == &s_tetris)
        return UI_ROTATION_R1;
    if (item->page.location == &s_game || item == &s_leveler || item == &s_accel_cal)
        return UI_ROTATION_R0;
    return UI_ROTATION_AUTO;
}

/* Called from foreground services, never modifies any ui_t directly.
 * Radio supersedes Powerbank if both connections arrive close together. */
void MiaoUi_RequestNavigate(ui_target_t target) {
    uint8_t request=target==UI_TARGET_RADIO?1u:
        target==UI_TARGET_POWERBANK?2u:0u;
    if(request && (!s_nav_pending || request<s_nav_pending)) {
        if(request==2u)s_power_nav_requested_at=Millis();
        s_nav_pending=request;
    }
}

uint8_t MiaoUi_NavigationReady(void) {
    /* Powerbank yields 100 ms to a possible higher-priority Radio request. */
    return (uint8_t)(s_nav_pending==1u ||
        (s_nav_pending==2u &&
         (uint32_t)(Millis()-s_power_nav_requested_at)>=100u));
}

void MiaoUi_ProcessNavigate(ui_t *ui) {
    ui_item_t *item;
    uint8_t request;
    if(!ui || !MiaoUi_NavigationReady())return;
    request=s_nav_pending;
    s_nav_pending=0u;
    if(!ui->navBookmark.valid)s_nav_active=0u;
    /* One bookmark: lower/equal priority never interrupts a temporary app. */
    if(ui->navBookmark.valid && s_nav_active && request>=s_nav_active)
        return;
    item=request==1u?&s_radio:&s_powerbank;
    if(UI_NavigateTo(ui,item,UI_NAV_PUSH))
        s_nav_active=request;
}

/* Only a confirmed new PB0 falling edge opens Radio, once per insertion.
 * PM first forwards EXTI sources; the foreground service waits 40 ms. */
/* Debounce the physical insert level after an EXTI falling edge.
 * Removal (high) re-arms each source without triggering navigation. */
void MiaoUi_ExternalEventService(uint8_t events) {
    uint32_t now=Millis();
    if(LL_GPIO_IsInputPinSet(MIC_INDET_GPIO_Port,MIC_INDET_Pin)) {
        s_mic_candidate=0u;
        s_mic_latched=0u;
    }
    else if((events&PM_WAKE_MIC_INDET) && !s_mic_latched) {
        s_mic_candidate=1u;
        s_mic_edge_at=now;
    }
    if(s_mic_candidate && (uint32_t)(now-s_mic_edge_at)>=40u) {
        s_mic_candidate=0u;
        if(!LL_GPIO_IsInputPinSet(MIC_INDET_GPIO_Port,MIC_INDET_Pin)) {
            s_mic_latched=1u;
            if(System_AutoRadioNavigationEnabled())
                MiaoUi_RequestNavigate(UI_TARGET_RADIO);
        }
    }

    if (PowerAlgo_AuxA_TakeInserted() && System_AutoAuxANavigationEnabled())
        MiaoUi_RequestNavigate(UI_TARGET_POWERBANK);
}

void MiaoUi_Setup(ui_t *ui) {
    s_nav_pending=0u;s_nav_active=0u;
    s_mic_candidate = 0u;
    s_mic_latched = 0u;
    Create_UI(ui,&s_powerbank);
    (void)UI_NavigateTo(ui, &s_powerbank, UI_NAV_REPLACE);
}
