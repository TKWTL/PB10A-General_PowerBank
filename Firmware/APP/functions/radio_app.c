/* Collar-Radio presentation/interaction only. All FM operations
 * live in radio_algo.c; low-level IRQ I2C lives in rda5807m.c.
 * The original blocking Radio_Run() loop becomes one MiaoUI frame.
 */
#include "radio_app.h"
#include "core/ui.h"
#include "radio_algo.h"
#include "power_algo.h"
#include "main.h"
#include "fonts/fonts.h"
#include "display/dispDriver.h"
#include "time_service.h"
#include "millis.h"
#include "mini_format.h"

#define RADIO_MENU_COUNT 5u
static uint8_t s_menu_open,s_menu_index,s_view_entered;
static int32_t s_slide_q,s_cursor_y_q,s_cursor_w_q;
static ui_animation_param_t s_slide_pid,s_cursor_y_pid,s_cursor_w_pid;
static radio_snapshot_t s_snapshot; /* read-only UI copy, no I2C in UI */

static void radio_copy_pid(ui_animation_param_t *dst,
    const ui_animation_param_t *ref) {
    *dst=*ref;
    dst->error=0;dst->sum_error=0;dst->last_error=0;
}

static void radio_enter_main(ui_t *ui) {
    s_menu_open=0u;
    s_slide_q=UI_FX(12); /* top starts 12px above; footer 12px below */
    radio_copy_pid(&s_slide_pid,&ui->animation.cursor_ani);
}

static void radio_enter_menu(ui_t *ui) {
    s_menu_index=0u;s_menu_open=1u;
    s_cursor_y_q=UI_FX(0);s_cursor_w_q=UI_FX(0);
    radio_copy_pid(&s_cursor_y_pid,&ui->animation.cursor_ani);
    radio_copy_pid(&s_cursor_w_pid,&ui->animation.optionbarPos_ani);
}

/* Collar-Radio radio.c: original XBM status icons (7x9, 6x9, 16x8). */
static const uint8_t s_signal_icon[]={
    0x7f,0x49,0x2a,0x1c,0x08,0x08,0x08,0x08,0x08
};
static const uint8_t s_speaker_icon[]={
    0x30,0x28,0x27,0x25,0x25,0x25,0x27,0x28,0x30
};
const uint8_t s_battery_icon[16] = {
    0xfc, 0xff, 0x04, 0x80, 0x07, 0x80, 0x05, 0x80, 0x05, 0x80,
    0x07, 0x80, 0x04, 0x80, 0xfc, 0xff};

/* Original x-axis maps 86.0-108.0 MHz to an approximately 0-127 marker. */
static uint8_t radio_marker_x(uint16_t frequency) {
    uint32_t x;
    if(frequency<=3440u)return 0u;
    x=((uint32_t)(frequency-3440u)*5u)/36u;
    return x>127u?127u:(uint8_t)x;
}


static uint8_t radio_has_station_view(uint16_t freq) {
    uint8_t i;
    for(i=0u;i<s_snapshot.station_count;i++)
        if(s_snapshot.stations[i]==freq)return 1u;
    return 0u;
}

/* Collar-Radio Radio_Run() visual structure, kept in its original positions:
 * top signal/headphone/volume/battery status, 24px centre frequency,
 * 43/45/47px dial and station markers, RTC time/date at bottom.
 * Battery bars and whole-icon low-capacity blink use the SW6306 cache.
 * This is a single-pass MiaoUI callback, not Collar's blocking while(1).
 */
/* Collar-Radio original geometry: PID-driven disp_value1 slides the
 * status icons upward from offscreen and the RTC footer downward into view.
 * Here s_slide_q persists between cooperative frames in fixed-point Q10. */
void Radio_AppDraw(ui_t *ui) {
    uint8_t fg,i,x;
    int16_t slide;
    char buf[24];
    uint16_t fraction;
    uint8_t align;
    power_monitor_t power;
    uint8_t bars;
    if(!ui)return;
    RadioAlgo_GetSnapshot(&s_snapshot);
    s_slide_q=UI_Animation(UI_FX(0),s_slide_q,&s_slide_pid);
    slide=(int16_t)UI_FXI(s_slide_q);
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(font_fusion_pixel_p12_zhcn);
    /* Collar-Radio radio.c: every top status widget uses y-slide. */
    Disp_DrawXBMP(1u,(uint16_t)(1-slide),7u,9u,s_signal_icon);
    (void)mini_snprintf(buf,sizeof(buf),":%u",(unsigned)s_snapshot.rssi);
    Disp_DrawStr(16u, (uint16_t)(10 - slide), buf);
    Disp_DrawStr(44u, (uint16_t)(10 - slide),
                 LL_GPIO_IsInputPinSet(MIC_INDET_GPIO_Port, MIC_INDET_Pin) ? "OFF" : "ON");
    Disp_DrawXBMP(69u,(uint16_t)(1-slide),6u,9u,s_speaker_icon);
    (void)mini_snprintf(buf,sizeof(buf),":%u%%",
        (unsigned)(((uint16_t)s_snapshot.volume+1u)*100u/16u));
    Disp_DrawStr(75u,(uint16_t)(10-slide),buf);
    /* Collar-Radio battery icon driven by the SW6306 cache. When pack
     * capacity <=5%, flash the entire icon; above 5%, show the outline
     * and 0/1/2/3/4 bars at >=10/30/50/70% respectively.
     * An invalid/stale cache cannot be treated as 0% capacity. */
    PowerAlgo_GetMonitor(&power);
    if(!(power.valid && power.capacity_valid && power.capacity_pct<=5u &&
        ((Millis()/800u)&1u)!=0u)) {
        Disp_DrawXBMP(111u,(uint16_t)(2-slide),16u,8u,s_battery_icon);
        if(power.valid && power.capacity_valid) {
            bars=power.capacity_pct>=70u?4u:
                power.capacity_pct>=50u?3u:
                power.capacity_pct>=30u?2u:
                power.capacity_pct>=10u?1u:0u;
            if(bars)
                Disp_DrawBox((uint16_t)(126u-3u*bars),
                    (uint16_t)(3-slide),(uint16_t)(3u*bars),6u);
        }
    }

    Disp_SetFont(font_radio_h24w12);
    if(s_snapshot.state==RADIO_STATE_SEARCH)
        Disp_DrawStr(4u,37u,"Seeking...");
    else if(s_snapshot.state==RADIO_STATE_RESET_WAIT || s_snapshot.state==RADIO_STATE_POWER_WAIT ||
        s_snapshot.state==RADIO_STATE_TUNE_WAIT)
    Disp_DrawStr(10u,37u,"Tuning...");
    else if(s_snapshot.state==RADIO_STATE_ERROR) {
        Disp_SetFont(font_fusion_pixel_p12_zhcn);
        (void)mini_snprintf(buf,sizeof(buf),"I2C ERROR %u",(unsigned)s_snapshot.error_code);
        Disp_DrawStr(21u,36u,buf);
    }
    else {
        /* A powered-down RDA5807 is not a display mode: preserve the
         * last tuned frequency and the existing station ruler while
         * OFF/AUTO is waiting for a headset. Headphone connection is
         * already shown in the top status row. Seek/Tune animations are
         * still shown above only while a search is really in progress. */
        fraction=(uint16_t)((s_snapshot.frequency%40u)*25u);
        if((fraction%10u)==0u)
            (void)mini_snprintf(buf,sizeof(buf),"%u.%02u",
            (unsigned)(s_snapshot.frequency/40u),
            (unsigned)(fraction/10u));
        else
            (void)mini_snprintf(buf,sizeof(buf),"%u.%03u",
            (unsigned)(s_snapshot.frequency/40u),(unsigned)fraction);
        /* Collar-Radio's width-dependent big-frequency/MHz alignment. */
        align=5u;
        if(s_snapshot.frequency>=4000u)++align;
        if(s_snapshot.frequency&1u)++align;
        Disp_DrawStr((uint16_t)(61u-align*6u),38u,buf);
        Disp_SetFont(font_fusion_pixel_p12_zhcn);
        Disp_DrawStr((uint16_t)(63u+align*6u),38u,"MHz");
    }

    /* Original 43/45/47 px station ruler and 25 kHz markers. */
    x=radio_marker_x(s_snapshot.frequency);
    Disp_DrawPixel(x,43u);
    Disp_DrawLine(0u,45u,127u,45u);
    for(i=0u;i<s_snapshot.station_count;i++) {
        uint8_t station_x=radio_marker_x(s_snapshot.stations[i]);
        Disp_DrawPixel(station_x,47u);
        if(s_snapshot.stations[i]==s_snapshot.frequency) {
            Disp_DrawPixel(station_x,46u);
            Disp_DrawPixel(station_x,48u);
        }
    }

    /* The original date/time footer moves in from y=63+12. */
    Disp_SetFont(font_fusion_pixel_p12_zhcn);
    (void)mini_snprintf(buf,sizeof(buf),"%02d:%02d:%02d",
        sTime.Hours,sTime.Minutes,sTime.Seconds);
    Disp_DrawStr(1u,(uint16_t)(63+slide),buf);
    (void)mini_snprintf(buf,sizeof(buf),"20%02d/%02d/%02d",
        sTime.Year,sTime.Month,sTime.Date);
    Disp_DrawStr(68u,(uint16_t)(63+slide),buf);
    Disp_SendBuffer();
}
/* Collar-Radio 5-row settings menu; no blocking while(1). */
/* The original five-row Collar-Radio menu, including its animated
 * 10-pixel XOR selection box (PID position AND PID text-width).
 * The earlier fixed full-row outline was not equivalent. */
static void radio_draw_menu(ui_t *ui) {
    static const char *const fixed_labels[RADIO_MENU_COUNT]={
        "Return","Power Mode:","Radio Mode:",0,"Exit Radio Page"
    };
    uint8_t i,fg,xor_color=2u;
    uint16_t width_target;
    const char *selected_label=0;
    char option[36];
    const char *mode=s_snapshot.mode==RADIO_MODE_SEARCH?"Search":
    s_snapshot.mode==RADIO_MODE_FREQ?"Frequency":"Preset";
    const char *power=s_snapshot.power_mode==RADIO_POWER_ON?"ON":
    s_snapshot.power_mode==RADIO_POWER_OFF?"OFF":"AUTO";
    if(!ui)return;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(font_fusion_pixel_p12_zhcn);
    Disp_DrawLine(0u,6u,127u,6u);
    Disp_DrawStr(19u,10u,"-Radio Setting:-");

    for(i=0u;i<RADIO_MENU_COUNT;i++) {
        const uint8_t y=(uint8_t)(20u+i*10u);
        if(i!=3u)Disp_DrawStr(2u,y,fixed_labels[i]);
        if(i==1u)Disp_DrawStr(74u,y,power);
        else if(i==2u)Disp_DrawStr(74u,y,mode);
        else if(i==3u) {
            if(s_snapshot.mode==RADIO_MODE_PRESET)
                (void)mini_snprintf(option,sizeof(option),"-All Clear and Seek-");
            else
                (void)mini_snprintf(option,sizeof(option),"%s",
                radio_has_station_view(s_snapshot.frequency)?
                "-Remove from List-":"-Add to station List-");
            Disp_DrawStr(1u,y,option);
        }
        if(i==s_menu_index)
            selected_label=i==3u?option:fixed_labels[i];
    }
    width_target=(uint16_t)(selected_label?
        Disp_GetUTF8Width(selected_label)+2u:2u);
    if(width_target>UI_HOR_RES)width_target=UI_HOR_RES;

    /* Collar's UI_Animation label-width + Y-position pair, with its
     * actual MiaoUI PID coefficients copied at menu entry. */
    s_cursor_w_q=UI_Animation(UI_FX(width_target),
        s_cursor_w_q,&s_cursor_w_pid);
    s_cursor_y_q=UI_Animation(UI_FX((int32_t)(10u+s_menu_index*10u)),
        s_cursor_y_q,&s_cursor_y_pid);
    Disp_SetDrawColor(&xor_color);
    Disp_DrawRBox(0u,(uint16_t)(UI_FXI(s_cursor_y_q)+1),
        (uint16_t)UI_FXI(s_cursor_w_q),10u,0u);
    Disp_SetDrawColor(&fg);
    Disp_SendBuffer();
}


/* A menu action only queues an algorithm command. The radio coroutine
 * then writes its own state and the BSP uses IRQ-backed I2C. */
static uint8_t radio_handle_menu(ui_t *ui,UI_ACTION action) {
    if(action==UI_ACTION_MINUS)
        s_menu_index=s_menu_index?(uint8_t)(s_menu_index-1u):
            (RADIO_MENU_COUNT-1u);
    else if(action==UI_ACTION_PLUS)
        s_menu_index=(uint8_t)((s_menu_index+1u)%RADIO_MENU_COUNT);
    else if(action==UI_ACTION_ENTER) {
        switch(s_menu_index) {
        case 0u:radio_enter_main(ui);break;
        case 1u:(void)RadioAlgo_Request(RADIO_CMD_POWER_CYCLE);break;
        case 2u:(void)RadioAlgo_Request(RADIO_CMD_MODE_CYCLE);break;
        case 3u:
            if(s_snapshot.mode==RADIO_MODE_PRESET) {
                (void)RadioAlgo_Request(RADIO_CMD_SCAN_ALL);
                radio_enter_main(ui);
            }
            else (void)RadioAlgo_Request(RADIO_CMD_TOGGLE_STATION);
            break;
        case 4u:return 1u;
        default:break;
        }
    }
    return 0u;
}
void Radio_AppOnLeave(ui_t *ui) {
    (void)ui;
    if(!s_view_entered)return;
    s_menu_open=0u;s_view_entered=0u;
    (void)RadioAlgo_Request(RADIO_CMD_VIEW_EXIT);
}
void Radio_AppRun(ui_t *ui) {
    UI_ACTION action;
    if(!ui)return;
    RadioAlgo_GetSnapshot(&s_snapshot);
    if(!s_view_entered) {
        s_view_entered=1u;
        radio_enter_main(ui);
        (void)RadioAlgo_Request(RADIO_CMD_VIEW_ENTER);
    }
    action=ui->action;
    if(action==UI_ACTION_BACK) {
        Radio_AppOnLeave(ui);
        return; /* Let MiaoUI leave the WORD page/bookmark. */
    }
    if(s_menu_open) {
        if(radio_handle_menu(ui,action)) {
            Radio_AppOnLeave(ui);
            ui->action=UI_ACTION_BACK;
            return;
        }
        ui->action=UI_ACTION_NONE;
        radio_draw_menu(ui);
        return;
    }
    if(action==UI_ACTION_ENTER) {
        radio_enter_menu(ui);
        ui->action=UI_ACTION_NONE;
        radio_draw_menu(ui);
        return;
    }
    if(action==UI_ACTION_MINUS)
        (void)RadioAlgo_Request(RADIO_CMD_VOLUME_DOWN);
    else if(action==UI_ACTION_PLUS)
        (void)RadioAlgo_Request(RADIO_CMD_VOLUME_UP);
    else if(action==UI_ACTION_UP)
        (void)RadioAlgo_Request(RADIO_CMD_FREQUENCY_DOWN);
    else if(action==UI_ACTION_DOWN)
        (void)RadioAlgo_Request(RADIO_CMD_FREQUENCY_UP);
    ui->action=UI_ACTION_NONE;
    Radio_AppDraw(ui);
}
