/* PB10A 128x64 power dashboard. All telemetry comes from foreground caches. */
#include "powerbank_app.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "indev/indevDriver.h"
#include "buttons.h"
#include "pm_api.h"
#include "beep_service.h"
#include "millis.h"
#include "mini_format.h"
#include "power_algo.h"
#include "radio_app.h"
#include "time_service.h"
#include "wled_algo.h"
#include <string.h>

#define PB_COL_BUS 2u
#define PB_COL_BAT 46u
#define PB_COL_AUX 90u

#define PB_ROW_TITLE 10u
#define PB_ROW_POWER 22u
#define PB_ROW_VOLTAGE 34u
#define PB_ROW_INFO 46u
#define PB_DIVIDER_Y 49u
#define PB_TOP_END 48u
#define PB_FOOTER_TOP 51u
#define PB_TIME_Y 57u
#define PB_DATE_Y 63u

#define PB_BUS_END 45u
#define PB_BAT_END 84u
#define PB_AUX_ARROW_X 85u
#define PB_DISCHARGE_ARROW_MW 100u
#define PB_BUS_CENTER (PB_COL_BUS + 18u)
#define PB_BAT_CENTER (PB_COL_BAT + 18u)
#define PB_AUX_CENTER (PB_COL_AUX + 18u)
#define PB_ANIM_START UI_FX(48)
#define PB_ANIM_FRAME_MS 5u
#define PB_STEADY_FRAME_MS 50u
#define PB_DIM_STEP_MS 80u

static int32_t s_remaining_q;
static ui_animation_param_t s_slide_pid;
static uint8_t s_active, s_dim_held;
static uint32_t s_dim_at;

static const char *short_protocol(const char *protocol) {
    static const struct {
        const char *name, *short_name;
    } aliases[] = {
        {"NONE", "5V"},
        {"PDFIX", "PD"},
        {"PDPPS", "PPS"},
        {"QC2.0", "QC2"},
        {"QC3.0", "QC3"},
        {"PE1.1", "PE1"},
        {"PE2.0", "PE2"},
        {"VOOC1", "VOOC"},
        {"VOOC4", "VOOC"},
        {"SVOOC", "SVOO"},
    };
    if (!protocol)
        return "UNK";
    for (unsigned i = 0u; i < sizeof(aliases) / sizeof(aliases[0]); ++i)
        if (strcmp(protocol, aliases[i].name) == 0)
            return aliases[i].short_name;
    return strlen(protocol) <= 4u ? protocol : "UNK";
}

static void draw_quantity(uint16_t x, uint16_t y, uint32_t milli, char unit, uint8_t valid) {
    char text[7];
    if (!valid || milli >= 999950u) {
        Disp_DrawStr(x, y, "--");
        return;
    }
    if (milli < 10000u) {
        (void)mini_snprintf(text, sizeof(text), "%u.%03u%c",
                            (unsigned)(milli / 1000u), (unsigned)(milli % 1000u), unit);
    } else if (milli < 99995u) {
        unsigned hundredths = (unsigned)((milli + 5u) / 10u);
        (void)mini_snprintf(text, sizeof(text), "%u.%02u%c", hundredths / 100u, hundredths % 100u, unit);
    } else {
        unsigned tenths = (unsigned)((milli + 50u) / 100u);
        (void)mini_snprintf(text, sizeof(text), "%u.%u%c", tenths / 10u, tenths % 10u, unit);
    }
    Disp_DrawStr(x, y, text);
}

static uint32_t power_mw(uint16_t mv, uint16_t ma) {
    return ((uint32_t)mv * ma + 500u) / 1000u;
}

static void draw_measurements(uint16_t x, uint16_t mv, uint16_t ma, uint8_t valid) {
    draw_quantity(x, PB_ROW_POWER, power_mw(mv, ma), 'W', valid);
    draw_quantity(x, PB_ROW_VOLTAGE, mv, 'V', valid);
}

/* Status fields use only the font's fixed six-pixel ASCII glyphs. */
static void draw_centered(uint16_t center, uint16_t y, const char *text) {
    Disp_DrawStr((uint16_t)(center - strlen(text) * 3u), y, text);
}

static void draw_bus(const power_monitor_t *power) {
    Disp_DrawStr(11u, PB_ROW_TITLE, "BUS");
    if (!power->valid) {
        draw_centered(PB_BUS_CENTER, PB_ROW_INFO, "--");
    } else {
        uint8_t c = power->c1_on || power->charging;
        if (power->charging)
            Disp_DrawStr(39u, PB_ROW_TITLE, ">");
        else if (power->discharging && (power->a1_on || power->c1_on) &&
                 power_mw(power->vbus_mv, power->ibus_ma) >= PB_DISCHARGE_ARROW_MW)
            Disp_DrawStr(39u, PB_ROW_TITLE, "<");
        if (power->a1_on && c) {
            draw_centered(PB_BUS_CENTER, PB_ROW_INFO, "A+C");
        } else if (power->a1_on || c) {
            char text[7];
            (void)mini_snprintf(text, sizeof(text), "%c:%s", c ? 'C' : 'A', short_protocol(power->protocol));
            draw_centered(PB_BUS_CENTER, PB_ROW_INFO, text);
        } else {
            draw_centered(PB_BUS_CENTER, PB_ROW_INFO, "NC");
        }
    }
    draw_measurements(PB_COL_BUS, power->vbus_mv, power->ibus_ma, power->valid);
}

static void draw_battery(const power_monitor_t *power, uint32_t now) {
    uint8_t valid = power->capacity_valid;
    if (!(valid && power->capacity_pct <= 5u && (now / 800u & 1u))) {
        Disp_DrawXBMP(56u, 2u, 16u, 8u, s_battery_icon);
        if (valid) {
            uint8_t pct = power->capacity_pct;
            unsigned bars = pct >= 70u ? 4u : pct >= 50u ? 3u
                                          : pct >= 30u   ? 2u
                                          : pct >= 10u   ? 1u
                                                         : 0u;
            if (bars)
                Disp_DrawBox((uint16_t)(71u - 3u * bars), 3u, (uint16_t)(3u * bars), 6u);
        }
    }
    if (valid) {
        char text[5];
        (void)mini_snprintf(text, sizeof(text), "%u%%", (unsigned)power->capacity_pct);
        draw_centered(PB_BAT_CENTER, PB_ROW_INFO, text);
    } else {
        draw_centered(PB_BAT_CENTER, PB_ROW_INFO, "--");
    }
    draw_measurements(PB_COL_BAT, power->vbat_mv, power->ibat_ma, power->valid);
}

static void draw_aux(const power_monitor_t *power, uint8_t bg, uint8_t fg) {
    if (power->aux_enabled) {
        Disp_DrawBox(PB_AUX_CENTER - 16u, 0u, 32u, 12u);
        Disp_SetDrawColor(&bg);
    }
    draw_centered(PB_AUX_CENTER, PB_ROW_TITLE, "AUX-A");
    Disp_SetDrawColor(&fg);
    if (power->aux_enabled && power->aux_valid &&
        power_mw(power->aux_mv, power->aux_ma) >= PB_DISCHARGE_ARROW_MW)
        Disp_DrawStr(PB_AUX_ARROW_X, PB_ROW_TITLE, ">");
    if (power->ntc_valid) {
        char text[8]; /* Up to six glyphs, with two bytes for °. */
        int c10 = power->ntc_c10;
        unsigned mag = (unsigned)(c10 < 0 ? -c10 : c10);
        const char *sign = c10 < 0 ? "-" : "";
        /* Drop the fractional digit when a sign or third digit needs room. */
        if (c10 <= -100 || c10 >= 1000)
            (void)mini_snprintf(text, sizeof(text), "%s%u°C", sign, (mag + 5u) / 10u);
        else
            (void)mini_snprintf(text, sizeof(text), "%s%u.%u°C", sign, mag / 10u, mag % 10u);
        Disp_DrawStr(PB_COL_AUX, PB_ROW_INFO, text);
    } else {
        Disp_DrawStr(PB_COL_AUX, PB_ROW_INFO, "--");
    }
    draw_measurements(PB_COL_AUX, power->aux_mv, power->aux_ma, power->aux_valid);
}

static void draw_footer(const power_monitor_t *power) {
    char text[20];
    uint8_t rtc_valid = RTC_CacheValid();
    if (power->eta_valid) {
        unsigned seconds = (unsigned)power->eta_seconds;
        if (seconds < 3600u)
            (void)mini_snprintf(text, sizeof(text), "ETA %02uM%02u", seconds / 60u, seconds % 60u);
        else
            (void)mini_snprintf(text, sizeof(text), "ETA %02uH%02u", seconds / 3600u, seconds / 60u % 60u);
        Disp_DrawStr(2u, PB_TIME_Y, text);
    } else {
        Disp_DrawStr(2u, PB_TIME_Y, "ETA --");
    }

    if (rtc_valid && power->eta_valid) {
        unsigned end = (unsigned)(sTime.Hours * 3600 + sTime.Minutes * 60 + sTime.Seconds) + power->eta_seconds;
        unsigned day = end / 86400u;
        unsigned hour = end / 3600u % 24u;
        unsigned minute = end / 60u % 60u;
        if (day)
            (void)mini_snprintf(text, sizeof(text), "END %02u:%02u+%u", hour, minute, day);
        else
            (void)mini_snprintf(text, sizeof(text), "END %02u:%02u", hour, minute);
        Disp_DrawStr(2u, PB_DATE_Y, text);
    } else {
        Disp_DrawStr(2u, PB_DATE_Y, "END --");
    }

    if (rtc_valid) {
        (void)mini_snprintf(text, sizeof(text), "%02d:%02d:%02d", sTime.Hours, sTime.Minutes, sTime.Seconds);
        Disp_DrawStr(96u, PB_TIME_Y, text);
        (void)mini_snprintf(text, sizeof(text), "20%02d/%02d/%02d", sTime.Year, sTime.Month, sTime.Date);
        Disp_DrawStr(88u, PB_DATE_Y, text);
    } else {
        Disp_DrawStr(96u, PB_TIME_Y, "--:--:--");
        Disp_DrawStr(88u, PB_DATE_Y, "----/--/--");
    }
}

/* Draw at valid logical coordinates, then shift only inside the region.
 * Reverse iteration when needed to preserve unread pixels in the DMA buffer. */
static void slide_region(int x0, int y0, int x1, int y1, int dx, int dy, uint8_t bg) {
    if (!dx && !dy)
        return;
    uint8_t *buf = Disp_GetBufferPtr();
    if (!buf)
        return;
    if (u8g2.cb == U8G2_R2) {
        int left = (int)UI_HOR_RES - x1;
        int top = (int)UI_VER_RES - y1;
        x1 = (int)UI_HOR_RES - x0;
        y1 = (int)UI_VER_RES - y0;
        x0 = left;
        y0 = top;
        dx = -dx;
        dy = -dy;
    }

    int xstep = dx > 0 ? -1 : 1;
    int ystep = dy > 0 ? -1 : 1;
    int xstop = dx > 0 ? x0 - 1 : x1;
    int ystop = dy > 0 ? y0 - 1 : y1;
    for (int y = dy > 0 ? y1 - 1 : y0; y != ystop; y += ystep) {
        int sy = y - dy;
        uint8_t bit = (uint8_t)(1u << (y & 7));
        uint8_t *row = buf + (y >> 3) * UI_HOR_RES;
        for (int x = dx > 0 ? x1 - 1 : x0; x != xstop; x += xstep) {
            int sx = x - dx;
            uint8_t on = bg;
            if (sx >= x0 && sx < x1 && sy >= y0 && sy < y1)
                on = buf[(sy >> 3) * UI_HOR_RES + sx] & (1u << (sy & 7));
            row[x] = on ? row[x] | bit : row[x] & (uint8_t)~bit;
        }
    }
}

static int displacement(int distance) {
    return (int)((s_remaining_q * distance + PB_ANIM_START / 2) / PB_ANIM_START);
}

uint8_t Powerbank_AppIsRunning(const ui_t *ui) {
    return ui && ui->nowItem && ui->nowItem->itemFunction == Powerbank_AppRun &&
           ui->fadePhase == 0u &&
           (ui->menuState == UI_ITEM_DRAWING || ui->menuState == UI_ITEM_RUNING);
}

void Powerbank_AppDimKeys(uint8_t held, uint32_t now) {
    const uint8_t left = 1u << KeyIndex_LEFT;
    const uint8_t right = 1u << KeyIndex_RIGHT;
    uint8_t first = held & (uint8_t)~s_dim_held;
    /* Track both physical presses even if their directions cancel.
     * Releasing LEFT while RIGHT stays held must never unlock RIGHT. */
    s_dim_held = held;
    if (held != left && held != right)
        return;
    if (!(first & held) && (uint32_t)(now - s_dim_at) < PB_DIM_STEP_MS)
        return;

    s_dim_at = now;
    WLED_AlgoRequestStep(held == right ? 1 : -1, (uint8_t)((first & held) == 0u));
    pm_api_refresh_idle();
    if (first & held)
        beep_key_click();
}

void Powerbank_AppOnLeave(ui_t *ui) {
    (void)ui;
    s_active = 0u;
}

void Powerbank_AppRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_ENTER || ui->action == UI_ACTION_BACK)
        return;
    /* DOWN toggles AUX. The 10 ms key service owns continuous dimming. */
    if (ui->action == UI_ACTION_PLUS && !indevActionIsRepeat())
        PowerAlgo_AuxA_Request(AUX_CMD_TOGGLE);
    ui->action = UI_ACTION_NONE;
    if (!s_active) {
        s_active = 1u;
        s_remaining_q = PB_ANIM_START;
        s_slide_pid = (ui_animation_param_t){.kp = 350, .kd = 32};
    }

    power_monitor_t power;
    PowerAlgo_GetDisplayMonitor(&power);
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetDrawColor(&fg);
    Disp_SetFont(font_fusion_pixel_p12_zhcn);
    Disp_SetClipWindow(0u, 0u, PB_BUS_END, PB_TOP_END);
    draw_bus(&power);
    Disp_SetClipWindow(PB_BUS_END, 0u, PB_BAT_END, PB_TOP_END);
    draw_battery(&power, Millis());
    Disp_SetClipWindow(PB_BAT_END, 0u, UI_HOR_RES, PB_TOP_END);
    draw_aux(&power, ui->bgColor, fg);
    Disp_SetFont(font_home_h6w4);
    Disp_SetClipWindow(0u, PB_FOOTER_TOP, UI_HOR_RES, UI_VER_RES);
    draw_footer(&power);
    Disp_SetMaxClipWindow();

    if (s_remaining_q > 0) {
        slide_region(0, 0, PB_BUS_END, PB_TOP_END, -displacement(46), 0, ui->bgColor);
        slide_region(PB_BUS_END, 0, PB_BAT_END, PB_TOP_END, 0, -displacement(48), ui->bgColor);
        slide_region(PB_BAT_END, 0, UI_HOR_RES, PB_TOP_END, displacement(43), 0, ui->bgColor);
        slide_region(0, PB_FOOTER_TOP, UI_HOR_RES, UI_VER_RES, 0, displacement(14), ui->bgColor);
        s_remaining_q = UI_Animation(0, s_remaining_q, &s_slide_pid);
        if (s_remaining_q < 0)
            s_remaining_q = 0;
        UI_RequestNextFrame(ui, PB_ANIM_FRAME_MS);
    } else {
        UI_RequestNextFrame(ui, PB_STEADY_FRAME_MS);
    }

    Disp_DrawLine(0u, PB_DIVIDER_Y, UI_HOR_RES - 1u, PB_DIVIDER_Y);
    uint16_t led_width = (uint16_t)(UI_HOR_RES * WLED_AlgoGetOutputPercent() / 100u);
    if (led_width) {
        Disp_DrawLine(0u, PB_DIVIDER_Y - 1u, led_width - 1u, PB_DIVIDER_Y - 1u);
        Disp_DrawLine(0u, PB_DIVIDER_Y + 1u, led_width - 1u, PB_DIVIDER_Y + 1u);
    }
    Disp_SendBuffer();
}
