/* Standard pressure altitude, relative zero and 5-second vertical speed. */
#include "altimeter_app.h"
#include "tools_motion_app.h"
#include "bmp180_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "pm_api.h"
#include "mini_math.h"
#include "mini_format.h"

/* Original PB10A altimeter icon, 30 x 30 XBM. */
// clang-format off
const unsigned char img_altimeter[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00,
    0x00, 0x3E, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00,
    0x00, 0x73, 0x18, 0x00, 0x80, 0x63, 0x3C, 0x00, 0x80, 0xE1, 0x3C, 0x00,
    0xC0, 0xC1, 0x7E, 0x00, 0xC0, 0xE7, 0x7F, 0x00, 0xE0, 0xFF, 0xE7, 0x00,
    0xE0, 0x99, 0xC7, 0x00, 0x70, 0x00, 0xC3, 0x01, 0x70, 0x00, 0x80, 0x01,
    0x38, 0x00, 0x80, 0x03, 0x38, 0x00, 0x00, 0x07, 0x18, 0x00, 0x00, 0x07,
    0x1C, 0x00, 0x00, 0x0E, 0x0C, 0x00, 0x00, 0x0E, 0x0E, 0x00, 0x00, 0x1C,
    0xFE, 0xFF, 0xFF, 0x1F, 0xFC, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on

#define ALTIMETER_FRAME_MS 200u
#define ALT_SPEED_WINDOW_MS 5000u
#define ALT_HISTORY_COUNT 12u

typedef struct {
    int32_t dm;
    uint32_t at_ms;
} altitude_point_t;

static altitude_point_t s_history[ALT_HISTORY_COUNT];
static uint8_t s_active, s_relative, s_reference_valid, s_seen;
static uint8_t s_count, s_next, s_speed_valid;
static int32_t s_reference_dm, s_speed_dmps;
static uint32_t s_seq;

/* Pick the nearest sample at least 5 seconds old and divide by the actual
 * elapsed time, not an assumed sampling period. One entry per BMP sample. */
static void altitude_speed_update(int32_t dm, uint32_t at_ms) {
    uint32_t best_age = UINT32_MAX;
    s_speed_valid = 0u;
    for (unsigned i = 0; i < s_count; ++i) {
        uint32_t age = at_ms - s_history[i].at_ms;
        if (age >= ALT_SPEED_WINDOW_MS && age < best_age) {
            best_age = age;
            s_speed_dmps = (dm - s_history[i].dm) * 1000 / (int32_t)age;
            s_speed_valid = 1u;
        }
    }
    s_history[s_next].dm = dm;
    s_history[s_next].at_ms = at_ms;
    s_next = (s_next + 1u) % ALT_HISTORY_COUNT;
    if (s_count < ALT_HISTORY_COUNT)
        ++s_count;
}

static void altitude_draw_height(int32_t dm, uint8_t valid) {
    if (!valid) {
        Disp_DrawStr(2u, 31u, "-- m");
        return;
    }
    char text[12];
    uint16_t x = 2u;
    /* The existing large font has digits and '.' but no minus glyph. */
    if (dm < 0) {
        Disp_DrawBox(x, 24u, 7u, 2u);
        x += 9u;
        dm = -dm;
    }
    UI_FmtScaled(text, sizeof(text), dm, 1u);
    Disp_SetFont(font_radio_h24w12);
    Disp_DrawStr(x, 35u, text);
    x += Disp_GetUTF8Width(text);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(x + 3u, 35u, "m");
}

void Altimeter_AppOnLeave(ui_t *ui) {
    (void)ui;
    if (!s_active)
        return;
    s_active = 0u;
    pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 0u);
    pm_api_refresh_idle();
}

void Altimeter_AppRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_BACK)
        return;
    if (!s_active) {
        s_active = 1u;
        s_relative = s_reference_valid = s_seen = 0u;
        s_count = s_next = s_speed_valid = 0u;
        pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
        pm_api_refresh_idle();
    }

    bmp180_sample_t sample;
    int32_t altitude_dm = 0;
    uint8_t sample_valid = BMP180Algo_GetSample(&sample);
    uint8_t valid = sample_valid && pressure_altitude_dm(sample.pressure_pa, &altitude_dm);
    if (valid) {
        if (!s_reference_valid) {
            s_reference_dm = altitude_dm;
            s_reference_valid = 1u;
        }
        if (!s_seen || s_seq != sample.seq) {
            s_seen = 1u;
            s_seq = sample.seq;
            altitude_speed_update(altitude_dm, sample.at_ms);
        }
    } else {
        s_seen = s_count = s_next = s_speed_valid = 0u;
    }
    if (ui->action == UI_ACTION_ENTER) {
        s_relative ^= 1u;
    } else if (valid && (ui->action == UI_ACTION_UP || ui->action == UI_ACTION_DOWN)) {
        /* Tools is an ICON page: physical LEFT/RIGHT map to UP/DOWN. */
        s_reference_dm = altitude_dm;
        s_relative = 1u;
    }
    ui->action = UI_ACTION_NONE;

    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(2u, 10u, s_relative ? "Rel Alt" : "Std Alt");
    Disp_SetFont(font_home_h6w4);
    Disp_DrawStr(84u, 10u, "CONF:Mode");
    Disp_SetFont(UI_FONT);
    altitude_draw_height(s_relative ? altitude_dm - s_reference_dm : altitude_dm, valid);
    Disp_DrawLine(0u, 38u, 127u, 38u);

    char text[16], value[12];
    if (sample_valid) {
        UI_FmtScaled(value, sizeof(value), sample.pressure_pa / 10, 1u);
        mini_snprintf(text, sizeof(text), "P:%shPa", value);
        Disp_DrawStr(0u, 49u, text);
        UI_FmtScaled(value, sizeof(value), sample.temperature_c10, 1u);
        mini_snprintf(text, sizeof(text), "T:%s°C", value);
        uint16_t width = Disp_GetUTF8Width(text);
        Disp_DrawStr(UI_HOR_RES - width, 49u, text);
    } else {
        Disp_DrawStr(0u, 49u, "P:--hPa");
        Disp_DrawStr(80u, 49u, "T:--°C");
    }
    if (s_speed_valid) {
        UI_FmtScaled(value, sizeof(value), s_speed_dmps, 1u);
        mini_snprintf(text, sizeof(text), "V:%sm/s", value);
    } else {
        mini_snprintf(text, sizeof(text), "V:--m/s");
    }
    Disp_DrawStr(0u, 62u, text);
    Disp_SetFont(font_home_h6w4);
    Disp_DrawStr(88u, 62u, "L/R:Zero");
    Disp_SetFont(UI_FONT);
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, ALTIMETER_FRAME_MS);
}
