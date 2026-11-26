/* Six guided magnetic poses: flat N, flipped flat N, screen-up N/E/S/W. */
#include "compass_cal_app.h"
#include "tools_motion_app.h"
#include "mmc5603_algo.h"
#include "sc7a20_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "pm_api.h"
#include "millis.h"
#include "mini_format.h"
#include "mini_math.h"
#include "i18n.h"

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
#define CAL_STALE_ABORT_MS 500u
#define CAL_SAMPLES 16u
#define CAL_MAG_SPAN_MG 50
#define CAL_FLAT_SPAN_MG 100
#define CAL_FLAT_BAD_SAMPLES 3u
#define CAL_STEPS 6u

/* UI order is independent of the algorithm's N,E,S,W,flat,flipped order. */
static const uint8_t s_face_order[CAL_STEPS] = {4u, 5u, 0u, 1u, 2u, 3u};
static uint8_t s_active, s_step, s_sampling, s_result;
static uint8_t s_waiting_data, s_accel_bad, s_retry;
static uint32_t s_mag_seq, s_accel_seq, s_stale_since;
static int16_t s_faces[CAL_STEPS][3];
static axis_capture_t s_capture;

static uint8_t pose_ok(const sc7a20_sample_t *a, uint8_t face, uint8_t holding) {
    /* Native SC7A20: Y- screen-up; Z- normally flat; Z+ flipped. */
    int32_t x = a->mg[0], y = a->mg[1], z = a->mg[2];
    uint32_t g2 = (uint32_t)(x * x) + (uint32_t)(y * y) + (uint32_t)(z * z);
    if (g2 < 750u * 750u || g2 > 1250u * 1250u)
        return 0u;
    int32_t along = face < 4u ? -y : face == 4u ? -z
                                                : z;
    if (along <= 0)
        return 0u;
    uint32_t transverse = g2 - (uint32_t)(along * along);
    /* Exclusive Q15 ratio bounds for the original atan2_cdeg limits
     * 5/8/10 degrees (including its interpolation rounding).
     * Reproduce floor(isqrt(transverse)) without actually taking a root. */
    uint32_t ratio = face < 4u ? 2868u : holding ? 5783u
                                                 : 4611u;
    uint32_t edge = ((ratio * (uint32_t)along - (uint32_t)along / 2u - 1u) >> 15) + 1u;
    return transverse < edge * edge;
}

static void cancel_capture(void) {
    s_sampling = 0u;
    s_capture.count = 0u;
    s_waiting_data = 0u;
    s_accel_bad = 0u;
    s_retry = 0u;
}

static void abort_capture(void) {
    cancel_capture();
    s_retry = 1u;
}

static void capture(const mmc5603_sample_t *m, uint8_t face) {
    if (m->seq == s_mag_seq)
        return;
    s_mag_seq = m->seq;
    int32_t span = face >= 4u ? CAL_FLAT_SPAN_MG : CAL_MAG_SPAN_MG;
    /* Yaw changes cannot be detected by gravity alone. */
    if (!axis_capture_add(&s_capture, m->mg, span)) {
        abort_capture();
        return;
    }
    if (s_capture.count != CAL_SAMPLES)
        return;
    axis_capture_mean(&s_capture, s_faces[face]);
    cancel_capture();
    if (++s_step == CAL_STEPS)
        s_result = MMC5603Algo_ApplySixFace(s_faces) ? 1u : 2u;
}

static void capture_update(const sc7a20_sample_t *a, uint8_t accel_valid,
                           const mmc5603_sample_t *m, uint8_t mag_valid, uint32_t now) {
    uint8_t face = s_face_order[s_step];
    uint8_t was_paused = s_waiting_data || s_accel_bad;

    if (!accel_valid || !mag_valid) {
        if (!s_waiting_data)
            s_stale_since = now;
        s_waiting_data = 1u;
        if ((uint32_t)(now - s_stale_since) >= CAL_STALE_ABORT_MS) {
            abort_capture();
            return;
        }
    } else {
        s_waiting_data = 0u;
    }

    uint8_t pose_valid = accel_valid && pose_ok(a, face, 1u);
    if (accel_valid && a->seq != s_accel_seq) {
        s_accel_seq = a->seq;
        if (pose_valid) {
            s_accel_bad = 0u;
        } else if (face < 4u || ++s_accel_bad >= CAL_FLAT_BAD_SAMPLES) {
            abort_capture();
            return;
        }
    }

    /* Preserve all accepted samples during a pause. Skip the latest
     * magnetic snapshot on recovery as it may have arrived while paused. */
    if (s_waiting_data || !pose_valid || was_paused) {
        if (mag_valid)
            s_mag_seq = m->seq;
        return;
    }
    capture(m, face);
}

static void center_text(uint16_t y, const char *text) {
    Disp_DrawCentered(y, text, 0u);
}

static void draw_cal(uint8_t data_valid, uint8_t pose_valid) {
    static const char *const directions[4] = {"North", "East", "South", "West"};
    uint8_t black = 0u, white = 1u;
    char text[22];
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&black);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&white);
    Disp_SetFont(UI_FONT);
    if (s_result) {
        center_text(25u, s_result == 1u ? "Calibration saved" : "Calibration error");
        center_text(40u, s_result == 1u ? "Any key: Exit" : "CONF: Retry");
        center_text(62u, s_result == 1u ? "CAL 6/6" : "Check headings");
    } else {
        uint8_t face = s_face_order[s_step];
        const char *title = face < 4u ? "Screen face up" : face == 4u ? "Lay flat"
                                                                      : "Upside Down";
        center_text(10u, title);
        /* Keep the arrow pointing screen-right. Under R2 that direction is
         * the opposite native edge: swap the requested heading instead,
         * so every captured face retains the algorithm's native heading. */
        unsigned direction_index = face < 4u ? face : 0u;
        if (Disp_GetRotation())
            direction_index = (direction_index + 2u) % 4u;
        Disp_DrawBox(17u, 24u, 42u, 3u);
        Disp_DrawLine(58u, 18u, 68u, 25u);
        Disp_DrawLine(58u, 32u, 68u, 25u);
        const char *direction = I18N_Text(directions[direction_index]);
        uint16_t width = Disp_GetUTF8Width(direction);
        Disp_DrawStr(74u + (width < 50u ? (50u - width) / 2u : 0u), 30u, direction);

        const char *prompt;
        if (!data_valid)
            prompt = "Waiting data";
        else if (!pose_valid)
            prompt = "Adjust position";
        else if (s_sampling)
            prompt = "Hold still";
        else if (s_retry)
            prompt = "Retry capture";
        else
            prompt = "Press CONF";
        center_text(46u, prompt);

        /* Progress/controls are deliberately at the bottom. */
        Disp_SetDrawColor(&black);
        Disp_DrawBox(0u, 52u, UI_HOR_RES, 12u);
        Disp_SetDrawColor(&white);
        Disp_DrawLine(0u, 52u, UI_HOR_RES - 1u, 52u);
        Disp_SetFont(font_home_h6w4);
        mini_snprintf(text, sizeof(text), "CAL %u/6 %u/16", (unsigned)(s_step + 1u), (unsigned)s_capture.count);
        Disp_DrawStr(2u, 62u, text);
        Disp_DrawStr(76u, 62u, !s_sampling && data_valid && pose_valid ? "L:Prev CONF" : "L:Prev");
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
    if (s_result == 1u && ui->action != UI_ACTION_NONE)
        return;

    sc7a20_sample_t a;
    mmc5603_sample_t m;
    uint32_t now = Millis();
    uint8_t accel_valid = SC7A20Algo_GetSample(&a) && (uint32_t)(now - a.at_ms) <= CAL_FRESH_MS;
    uint8_t mag_valid = MMC5603Algo_GetSample(&m) && (uint32_t)(now - m.at_ms) <= CAL_FRESH_MS;

    if (ui->action == UI_ACTION_UP && !s_result) { /* screen LEFT, previous */
        cancel_capture();
        if (s_step)
            --s_step;
    } else if (ui->action == UI_ACTION_ENTER) {
        if (s_result == 2u) {
            s_step = s_result = 0u;
            cancel_capture();
        } else if (!s_result && !s_sampling && accel_valid && mag_valid &&
                   pose_ok(&a, s_face_order[s_step], 0u)) {
            cancel_capture();
            s_mag_seq = m.seq; /* only collect new, independent samples */
            s_accel_seq = a.seq;
            s_sampling = 1u;
        }
    }
    ui->action = UI_ACTION_NONE;
    if (s_sampling)
        capture_update(&a, accel_valid, &m, mag_valid, now);

    /* Recompute after advancing, canceling or backing up a pose. */
    uint8_t pose_valid = !s_result && accel_valid &&
                         pose_ok(&a, s_face_order[s_step], s_sampling);
    draw_cal(accel_valid && mag_valid, pose_valid);
    UI_RequestNextFrame(ui, CAL_FRAME_MS);
}
