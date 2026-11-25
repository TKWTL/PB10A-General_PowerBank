/* Six guided magnetic poses: screen-up N/E/S/W, flat N, flipped flat N. */
#include "compass_cal_app.h"
#include "tools_motion_app.h"
#include "mmc5603_algo.h"
#include "sc7a20_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "pm_api.h"
#include "millis.h"
#include "mini_math.h"
#include "mini_format.h"

/* Original PB10A calibration icon, 30x30 XBM. */
// clang-format off
const unsigned char img_compass_cal[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x80, 0x00,
    0x00, 0xF0, 0xA0, 0x06, 0x00, 0xE0, 0xE1, 0x03, 0x00, 0xCC, 0x41, 0x0E,
    0x00, 0x9E, 0x70, 0x02, 0x00, 0x3F, 0xC0, 0x07, 0x80, 0x3F, 0x60, 0x05,
    0xC0, 0x1F, 0x00, 0x01, 0xE0, 0x0F, 0x00, 0x00, 0xF0, 0x07, 0x00, 0x00,
    0xF8, 0x03, 0x00, 0x00, 0xF8, 0x01, 0x00, 0x03, 0xF8, 0x01, 0x80, 0x07,
    0xFC, 0x00, 0x00, 0x0F, 0xFC, 0x00, 0x60, 0x0E, 0xFC, 0x00, 0xF0, 0x04,
    0xFC, 0x00, 0xF8, 0x01, 0xF8, 0x01, 0xFC, 0x01, 0xF8, 0x01, 0xFE, 0x00,
    0xF8, 0x87, 0x7F, 0x00, 0xF0, 0xFF, 0x3F, 0x00, 0xE0, 0xFF, 0x1F, 0x00,
    0xC0, 0xFF, 0x0F, 0x00, 0x80, 0xFF, 0x07, 0x00, 0x00, 0xFF, 0x03, 0x00,
    0x00, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on


#define CAL_FRAME_MS 40u
#define CAL_FRESH_MS 200u
#define CAL_SAMPLES 16u
#define CAL_MAX_TILT_CDEG 500
#define CAL_MAG_SPAN_MG 50
#define CAL_STEPS 6u

static uint8_t s_active, s_step, s_count, s_sampling, s_result;
static uint32_t s_mag_seq;
static int16_t s_faces[CAL_STEPS][3];
static int32_t s_sum[3], s_min[3], s_max[3];

static uint8_t pose_ok(const sc7a20_sample_t *a, uint8_t step) {
    /* Native SC7A20: Y- screen-up; Z- normally flat; Z+ flipped. */
    int32_t x = a->mg[0], y = a->mg[1], z = a->mg[2];
    uint32_t g2 = (uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z);
    if (g2 < 750u * 750u || g2 > 1250u * 1250u)
        return 0u;
    int32_t along = step < 4u ? -y : step == 4u ? -z
                                                : z;
    uint32_t transverse = g2 - (uint32_t)(along * along);
    return along > 0 && atan2_cdeg((int32_t)isqrt_u32(transverse), along) <= CAL_MAX_TILT_CDEG;
}

static void cancel_capture(void) {
    s_sampling = 0u;
    s_count = 0u;
}

static void capture(const mmc5603_sample_t *m) {
    if (m->seq == s_mag_seq)
        return;
    s_mag_seq = m->seq;
    for (unsigned i = 0u; i < 3u; ++i) {
        int32_t v = m->mg[i];
        if (!s_count) {
            s_sum[i] = 0;
            s_min[i] = s_max[i] = v;
        } else {
            if (v < s_min[i])
                s_min[i] = v;
            if (v > s_max[i])
                s_max[i] = v;
            /* Yaw changes cannot be detected by gravity alone. */
            if (s_max[i] - s_min[i] > CAL_MAG_SPAN_MG) {
                cancel_capture();
                return;
            }
        }
        s_sum[i] += v;
    }
    if (++s_count != CAL_SAMPLES)
        return;
    for (unsigned i = 0u; i < 3u; ++i)
        s_faces[s_step][i] = (int16_t)(s_sum[i] / (int32_t)CAL_SAMPLES);
    cancel_capture();
    if (++s_step == CAL_STEPS)
        s_result = MMC5603Algo_ApplySixFace(s_faces) ? 1u : 2u;
}

static void center_text(uint16_t y, const char *text) {
    uint16_t width = Disp_GetUTF8Width(text);
    Disp_DrawStr(width < UI_HOR_RES ? (UI_HOR_RES - width) / 2u : 0u, y, text);
}

static void draw_cal(const sc7a20_sample_t *a, uint8_t accel_valid, uint8_t ready) {
    static const char *const directions[4] = {"N", "E", "S", "W"};
    uint8_t black = 0u, white = 1u;
    char text[22];
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&black);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&white);
    Disp_SetFont(UI_FONT);
    if (s_result) {
        center_text(25u, s_result == 1u ? "Calibration saved" : "Calibration error");
        center_text(40u, s_result == 1u ? "Hold CONF: Exit" : "CONF: Retry");
        center_text(62u, s_result == 1u ? "CAL 6/6" : "Check headings");
    } else {
        if (s_step < 4u) {
            center_text(10u, "Screen face up");
            /* The physical reference is always the screen's right edge. */
            Disp_DrawBox(17u, 24u, 42u, 3u);
            Disp_DrawLine(58u, 18u, 68u, 25u);
            Disp_DrawLine(58u, 32u, 68u, 25u);
            Disp_DrawStr(84u, 30u, directions[s_step]);
            center_text(46u, s_sampling ? "Hold still" : ready ? "Move phone away"
                                                               : "Adjust position");
        } else {
            if (accel_valid) {
                Disp_SetClipWindow(0u, 0u, UI_HOR_RES, 51u);
                Motion_DrawHorizon(a->level_mg);
                Disp_SetMaxClipWindow();
            }
            /* Text strips stay legible against the shared white/black horizon. */
            Disp_SetDrawColor(&black);
            Disp_DrawBox(0u, 0u, UI_HOR_RES, 13u);
            Disp_DrawBox(0u, 39u, UI_HOR_RES, 13u);
            Disp_SetDrawColor(&white);
            center_text(10u, s_step == 4u ? "Flat - right to N" : "Flip long axis");
            center_text(49u, s_sampling ? "Hold still" : s_step == 5u ? "Keep right to N"
                                                     : ready          ? "Hold still"
                                                                      : "Adjust position");
        }
        /* Progress/controls are deliberately at the bottom. */
        Disp_SetDrawColor(&black);
        Disp_DrawBox(0u, 52u, UI_HOR_RES, 12u);
        Disp_SetDrawColor(&white);
        Disp_DrawLine(0u, 52u, UI_HOR_RES - 1u, 52u);
        Disp_SetFont(font_home_h6w4);
        mini_snprintf(text, sizeof(text), "CAL %u/6 %u/16", (unsigned)(s_step + 1u), (unsigned)s_count);
        Disp_DrawStr(2u, 62u, text);
        Disp_DrawStr(76u, 62u, s_sampling ? "Hold still" : ready ? "L:Prev CONF"
                                                                 : "L:Prev Wait");
        Disp_SetFont(UI_FONT);
    }
    Disp_SendBuffer();
}

void CompassCal_AppOnLeave(ui_t *ui) {
    (void)ui;
    if (!s_active)
        return;
    s_active = 0u;
    cancel_capture();
    MMC5603Algo_SetToolMode(0u);
    SC7A20Algo_SetToolMode(0u);
    pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 0u);
    pm_api_refresh_idle();
}

void CompassCal_AppRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_BACK)
        return;
    if (!s_active) {
        s_active = 1u;
        s_step = s_result = 0u;
        cancel_capture();
        pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
        pm_api_refresh_idle();
        MMC5603Algo_SetToolMode(1u);
        SC7A20Algo_SetToolMode(3u);
    }
    sc7a20_sample_t a;
    mmc5603_sample_t m;
    uint32_t now = Millis();
    uint8_t accel_valid = SC7A20Algo_GetSample(&a) && (uint32_t)(now - a.at_ms) <= CAL_FRESH_MS;
    uint8_t mag_valid = MMC5603Algo_GetSample(&m) && (uint32_t)(now - m.at_ms) <= CAL_FRESH_MS;
    uint8_t ready = !s_result && accel_valid && mag_valid && pose_ok(&a, s_step);

    if (ui->action == UI_ACTION_UP && !s_result) { /* physical LEFT, previous */
        cancel_capture();
        if (s_step)
            --s_step;
    } else if (ui->action == UI_ACTION_ENTER) {
        if (s_result == 2u) {
            s_step = s_result = 0u;
            cancel_capture();
        } else if (ready && !s_sampling) {
            s_count = 0u;
            s_mag_seq = m.seq; /* only collect new, independent samples */
            s_sampling = 1u;
        }
    }
    ui->action = UI_ACTION_NONE;
    if (s_sampling) {
        if (!ready)
            cancel_capture();
        else
            capture(&m);
    }
    /* Recompute after advancing or backing up a pose. */
    ready = !s_result && accel_valid && mag_valid && pose_ok(&a, s_step);
    draw_cal(&a, accel_valid, ready);
    UI_RequestNextFrame(ui, CAL_FRAME_MS);
}
