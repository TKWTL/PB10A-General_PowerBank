/* Magnetic north, gravity projection and vector filtering, all integer. */
#include "compass_app.h"
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
#include "i18n.h"

/* Original PB10A compass icon, 30 x 30 XBM. */
// clang-format off
const unsigned char img_compass[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xF0, 0x03, 0x00, 0x00, 0xFE, 0x1F, 0x00, 0x00, 0xCF, 0x3C, 0x00,
    0x80, 0x03, 0x70, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01,
    0x70, 0x00, 0x90, 0x03, 0x30, 0x00, 0x0C, 0x03, 0x30, 0x00, 0x0F, 0x03,
    0x18, 0xC0, 0x07, 0x06, 0x18, 0xE0, 0x07, 0x06, 0x38, 0x30, 0x03, 0x07,
    0x38, 0x30, 0x03, 0x07, 0x18, 0xF8, 0x01, 0x06, 0x18, 0xF8, 0x00, 0x06,
    0x30, 0x3C, 0x00, 0x03, 0x30, 0x0C, 0x00, 0x03, 0x70, 0x02, 0x80, 0x03,
    0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0x03, 0x70, 0x00,
    0x00, 0xCF, 0x3C, 0x00, 0x00, 0xFE, 0x1F, 0x00, 0x00, 0xF0, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on



#define COMPASS_FRAME_MS 40u
#define COMPASS_FRESH_MS 200u
#define COMPASS_FILTER_SCALE 16
#define COMPASS_MOTION_MG 250
#define COMPASS_MIN_HORIZONTAL_MG 20

static uint8_t s_active, s_mag_seen, s_accel_seen, s_accel_quiet;
static uint32_t s_mag_seq, s_accel_seq;
static int32_t s_mag_q4[3], s_accel[3];

/* Accelerometer input is int16_t; magnetic vector is scaled before norm. */
static uint32_t norm_sq(const int32_t v[3]) {
    return (uint32_t)(v[0] * v[0]) + (uint32_t)(v[1] * v[1]) + (uint32_t)(v[2] * v[2]);
}

/* Native acceleration measures UP. Screen-right is native -X.
 * R = F x A, F_h = A x R / |A|, F = (-1,0,0).
 * The equal-length horizontal vectors give atan2(-B.R, B.F_h). */
static uint8_t compass_heading(const int32_t a[3], const int32_t b[3], uint16_t *heading) {
    uint32_t gravity_sq = norm_sq(a);
    if (gravity_sq < 750u * 750u || gravity_sq > 1250u * 1250u)
        return 0u;
    int32_t gravity = (int32_t)isqrt_u32(gravity_sq);
    int32_t yz_sq = a[1] * a[1] + a[2] * a[2];
    int32_t horizontal = (int32_t)isqrt_u32((uint32_t)yz_sq);
    if (horizontal * 4 < gravity)
        return 0u; /* screen-right projection is nearly vertical */

    int32_t forward = -b[0] * (yz_sq / gravity) +
                      b[1] * (a[0] * a[1] / gravity) +
                      b[2] * (a[0] * a[2] / gravity);
    int32_t right = b[1] * a[2] - b[2] * a[1];
    int32_t fh = forward / horizontal, rh = right / horizontal;
    if (fh > -COMPASS_MIN_HORIZONTAL_MG && fh < COMPASS_MIN_HORIZONTAL_MG &&
        rh > -COMPASS_MIN_HORIZONTAL_MG && rh < COMPASS_MIN_HORIZONTAL_MG &&
        fh * fh + rh * rh < COMPASS_MIN_HORIZONTAL_MG * COMPASS_MIN_HORIZONTAL_MG)
        return 0u;

    int32_t angle = atan2_cdeg(-right, forward);
    if (angle < 0)
        angle += 36000;
    *heading = (uint16_t)(((angle + 50) / 100) % 360);
    return 1u;
}

/* Raw minus bias can exceed int16_t. Scale before squaring and restore
 * the magnitude to avoid 64-bit arithmetic and signed overflow. */
static uint32_t compass_strength(const int32_t b[3]) {
    int32_t v[3] = {b[0], b[1], b[2]};
    uint32_t scale = 1u;
    while (v[0] > 32767 || v[0] < -32767 || v[1] > 32767 || v[1] < -32767 || v[2] > 32767 || v[2] < -32767) {
        for (unsigned i = 0u; i < 3u; ++i)
            v[i] /= 2;
        scale *= 2u;
    }
    return isqrt_u32(norm_sq(v)) * scale;
}

static uint8_t compass_magnetic(int32_t b[3], uint32_t now) {
    static const int8_t axes[3] = {COMPASS_MAG_X_AXIS, COMPASS_MAG_Y_AXIS, COMPASS_MAG_Z_AXIS};
    mmc5603_sample_t sample;
    if (!MMC5603Algo_GetCorrectedSample(&sample) || (uint32_t)(now - sample.at_ms) > COMPASS_FRESH_MS) {
        s_mag_seen = 0u;
        return 0u;
    }
    if (!s_mag_seen || s_mag_seq != sample.seq) {
        for (unsigned i = 0; i < 3u; ++i) {
            int axis = axes[i];
            int32_t value = (axis < 0 ? -sample.mg[-axis - 1] : sample.mg[axis - 1]) * COMPASS_FILTER_SCALE;
            if (s_mag_seen)
                s_mag_q4[i] += (value - s_mag_q4[i]) / 5;
            else
                s_mag_q4[i] = value;
        }
        s_mag_seen = 1u;
        s_mag_seq = sample.seq;
    }
    for (unsigned i = 0; i < 3u; ++i)
        b[i] = s_mag_q4[i] / COMPASS_FILTER_SCALE;
    return 1u;
}

static uint8_t compass_gravity(uint32_t now) {
    sc7a20_sample_t sample;
    if (!SC7A20Algo_GetSample(&sample) || (uint32_t)(now - sample.at_ms) > COMPASS_FRESH_MS) {
        s_accel_seen = 0u;
        return 0u;
    }
    if (!s_accel_seen || s_accel_seq != sample.seq) {
        s_accel_quiet = 1u;
        for (unsigned i = 0; i < 3u; ++i) {
            int32_t delta = sample.mg[i] - s_accel[i];
            if (s_accel_seen && (delta > COMPASS_MOTION_MG || delta < -COMPASS_MOTION_MG))
                s_accel_quiet = 0u;
            s_accel[i] = sample.mg[i];
        }
        s_accel_seen = 1u;
        s_accel_seq = sample.seq;
    }
    return s_accel_quiet;
}

/* Only the dial geometry needs sin/cos. Quarter-wave Q8, 5-degree nodes;
 * linear interpolation keeps a turning tick within a pixel of its target. */
static int16_t dial_sin(unsigned angle) {
    static const uint16_t quarter[19] = {
        0, 22, 44, 66, 88, 108, 128, 147, 165, 181, 196, 210, 222, 232, 241, 247, 252, 255, 256};
    uint8_t negative = angle >= 180u;
    angle %= 180u;
    if (angle > 90u)
        angle = 180u - angle;
    unsigned index = angle / 5u;
    int16_t sine = (int16_t)quarter[index];
    if (index < 18u)
        sine += (int16_t)((quarter[index + 1u] - quarter[index]) * (angle % 5u) / 5u);
    return negative ? -sine : sine;
}

static void dial_point(unsigned angle, int radius, uint16_t *x, uint16_t *y) {
    *x = (uint16_t)(31 + dial_sin(angle) * radius / 256);
    *y = (uint16_t)(31 - dial_sin((angle + 90u) % 360u) * radius / 256);
}

static void compass_dial(uint16_t heading, uint8_t valid) {
    static const char *const cardinal[] = {"N", "E", "S", "W"};
    Disp_DrawCircle(31u, 31u, 29u, U8G2_DRAW_ALL);
    Disp_SetFont(UI_FONT);
    if (valid) {
        for (unsigned bearing = 0u; bearing < 360u; bearing += 30u) {
            unsigned angle = (bearing + 450u - heading) % 360u;
            uint16_t x1, y1, x2, y2;
            dial_point(angle, 27, &x1, &y1);
            dial_point(angle, bearing % 90u ? 24 : 22, &x2, &y2);
            Disp_DrawLine(x1, y1, x2, y2);
            if (bearing % 90u == 0u) {
                dial_point(angle, 15, &x1, &y1);
                const char *label = I18N_Text(cardinal[bearing / 90u]);
                Disp_DrawStr(x1 - Disp_GetUTF8Width(label) / 2u, y1 + 4u, label);
            }
        }
    } else {
        Disp_DrawStr(28u, 35u, "?");
    }
    /* Fixed index at display right; the rose turns underneath. */
    Disp_DrawLine(61u, 28u, 55u, 31u);
    Disp_DrawLine(61u, 34u, 55u, 31u);
    Disp_DrawLine(61u, 28u, 61u, 34u);
}

void Compass_AppOnLeave(ui_t *ui) {
    (void)ui;
    if (!s_active)
        return;
    s_active = 0u;
    MMC5603Algo_SetToolMode(0u);
    SC7A20Algo_SetToolMode(0u);
    pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 0u);
    pm_api_refresh_idle();
}

void Compass_AppRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_ENTER || ui->action == UI_ACTION_BACK)
        return;
    ui->action = UI_ACTION_NONE;
    if (!s_active) {
        s_active = 1u;
        s_mag_seen = s_accel_seen = 0u;
        pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
        pm_api_refresh_idle();
        MMC5603Algo_SetToolMode(1u);
        SC7A20Algo_SetToolMode(3u);
    }

    uint32_t now = Millis();
    int32_t b[3];
    uint16_t heading = 0u;
    uint8_t magnetic_valid = compass_magnetic(b, now);
    uint8_t gravity_valid = compass_gravity(now);
    uint8_t valid = magnetic_valid && gravity_valid && compass_heading(s_accel, b, &heading);
    if (valid && Disp_GetRotation())
        heading = (heading + 180u) % 360u;

    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&fg);
    compass_dial(heading, valid);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(68u, 11u, I18N_Text(MMC5603Algo_IsCalibrated() ? "Mag North" : "Uncalibr."));
    char text[16];
    if (valid) {
        static const char *const directions[] = {
            "North", "North East", "East", "South East", "South", "South West", "West", "North West"};
        mini_snprintf(text, sizeof(text), "%03u", (unsigned)heading);
        Disp_SetFont(font_radio_h24w12);
        Disp_DrawStr(72u, 36u, text);
        Disp_SetFont(UI_FONT);
        Disp_DrawStr(108u, 25u, "°");
        const char *direction = I18N_Text(directions[((heading + 22u) / 45u) & 7u]);
        uint16_t width = Disp_GetUTF8Width(direction);
        Disp_DrawStr(64u + (width < 64u ? (64u - width) / 2u : 0u), 49u, direction);
    } else {
        Disp_DrawStr(82u, 31u, "---°");
        Disp_DrawStr(73u, 49u, I18N_Text("No fix"));
    }
    if (magnetic_valid)
        mini_snprintf(text, sizeof(text), "B:%u mG", (unsigned)compass_strength(b));
    else
        mini_snprintf(text, sizeof(text), "B:-- mG");
    Disp_DrawStr(67u, 62u, text);
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, COMPASS_FRAME_MS);
}
