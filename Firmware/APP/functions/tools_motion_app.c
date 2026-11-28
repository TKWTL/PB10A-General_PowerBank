/* Ported from PB02B-Pocket_Powerbank:
 *   firmware/Applications/functions/leveler.c
 *   firmware/Applications/functions/gravity_calibrate.c
 * The original 160x40 LVGL application is reworked for PB10A's 128x64
 * MiaoUI/u8g2 with no canvas allocation and no I2C access in the UI.
 * Horizon, gravity-dependent displacement, 43/47-degree mode hysteresis
 * and XOR zenith circles follow the original mathematical model.
 */
#include "tools_motion_app.h"
#include "sc7a20_algo.h"
#include "display/dispDriver.h"
#include "pm_api.h"
#include "millis.h"
#include "mini_math.h"
#include "mini_format.h"
#include "core/ui.h"
#include "i18n.h"
#include <string.h>

// clang-format off
/* Collar-Radio Application/MiaoUI/images/image.c: 30x30 GPL-3.0 XBM. */
const unsigned char img_leveler[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x30,0x00,
    0x00,0x00,0x7c,0x00,0x00,0x00,0xee,0x00,0x00,0x00,0xc7,0x01,0x00,0x80,0x83,0x03,
    0x00,0xc0,0x11,0x07,0x00,0xe0,0x38,0x06,0x00,0xf0,0x11,0x03,0x00,0xb8,0x81,0x03,
    0x00,0x9c,0xc1,0x01,0x00,0xce,0xe1,0x00,0x00,0xe7,0x70,0x00,0x80,0x7f,0x38,0x00,
    0xc0,0x3d,0x1c,0x00,0xe0,0x00,0x0e,0x00,0x70,0x00,0x07,0x00,0x30,0x82,0x03,0x00,
    0x18,0xc7,0x01,0x00,0x38,0xe2,0x00,0x00,0x70,0x70,0x00,0x00,0xe0,0x38,0x00,0x00,
    0xc0,0x1d,0x00,0x00,0x80,0x0f,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

/* Collar-Radio Application/MiaoUI/images/image.c: 30x30 GPL-3.0 XBM. */
const unsigned char img_leveler_cal[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x30, 0x00, 0xA0, 0x05,
    0x78, 0x00, 0xF0, 0x0F, 0xFC, 0x00, 0xE0, 0x07, 0x30, 0x00, 0x78, 0x1E,
    0x30, 0x00, 0x78, 0x1E, 0x30, 0x00, 0xE0, 0x07, 0x30, 0x00, 0xF0, 0x0F,
    0x30, 0x00, 0xA0, 0x05, 0x30, 0x00, 0x80, 0x01, 0x30, 0x80, 0x07, 0x00,
    0x30, 0x00, 0x07, 0x00, 0x30, 0x80, 0x07, 0x00, 0x30, 0xC0, 0x05, 0x00,
    0x30, 0xE0, 0x00, 0x00, 0x30, 0x70, 0x00, 0x00, 0x30, 0x38, 0x00, 0x00,
    0x30, 0x1C, 0x00, 0x00, 0x30, 0x0E, 0x00, 0x00, 0x30, 0x07, 0x00, 0x00,
    0xB0, 0x03, 0x00, 0x00, 0xF0, 0x01, 0x00, 0x02, 0xF0, 0x00, 0x00, 0x06,
    0xF0, 0xFF, 0xFF, 0x0F, 0xF0, 0xFF, 0xFF, 0x0F, 0x00, 0x00, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on

#define LEVEL_VIEW_MS       40u
#define LEVEL_PLUMB_ENTER   4300
#define LEVEL_PLUMB_EXIT    4700
#define LEVEL_MG_SCALE      16
#define LEVEL_PIXEL_SCALE   256
#define LEVEL_RADIUS        18 /* 36px diameter, center at half pixels */
#define CAL_VIEW_MS         200u
#define CAL_HOLD_INTERVALS  10u
#define CAL_AXIS_MIN_ABS    700
#define CAL_OTHER_MAX_ABS   200
#define CAL_STABLE_RANGE    100
#define CAL_FACE_COUNT      6u
#define CAL_FACE_NONE       0xFFu

static uint8_t s_level_active,s_level_valid,s_level_mode;
static uint32_t s_level_seq,s_level_draw_at;
static int32_t s_level_x, s_level_y, s_level_z; /* mg in Q4 */

typedef enum { CAL_CLOSED,
               CAL_INTRO,
               CAL_CAPTURING,
               CAL_SAVED,
               CAL_FAILED } cal_state_t;
static cal_state_t s_cal_state;
static uint8_t s_cal_done[CAL_FACE_COUNT], s_cal_candidate;
static int16_t s_cal_faces[CAL_FACE_COUNT][3];
static axis_capture_t s_cal_capture;
static uint32_t s_cal_seq,s_cal_sample_at,s_cal_complete_at;

static int16_t cal_abs(int16_t x) {
    return (int16_t)(x<0 ? -(int32_t)x : x);
}
static void motion_tool_begin(uint8_t mode) {
    pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
    pm_api_refresh_idle();
    SC7A20Algo_SetToolMode(mode);
}
static void motion_tool_end(void) {
    SC7A20Algo_SetToolMode(0u);
    pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 0u);
    pm_api_refresh_idle();
}
void Motion_LevelerOnLeave(ui_t *ui) {
    (void)ui;
    if(!s_level_active)return;
    s_level_active=0u;
    motion_tool_end();
}
void Motion_CalibrationOnLeave(ui_t *ui) {
    (void)ui;
    if (s_cal_state == CAL_CLOSED)
        return;
    s_cal_state = CAL_CLOSED;
    motion_tool_end();
}
static void draw_black(void) {
    uint8_t black=0u;
    Disp_SetMaxClipWindow();
    Disp_ClearBuffer();
    Disp_SetDrawColor(&black);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
}
static uint32_t level_abs(int32_t value) {
    return (uint32_t)(value < 0 ? -value : value);
}

static void level_draw_horizon(int32_t x, int32_t y, int32_t h, int32_t z) {
    int16_t row, first, last, cross;
    uint8_t white = 1u, xor_color = 2u;
    if (h == 0) return;

    /* Multiply the original line equation by the XY magnitude. X and Y
     * use the scaled vector, Z is Q4 milligravity. Split Z at the mg
     * boundary so the product fits in 32 bits without losing its fraction.
     * Upright X < 0 is white above, positive Z moves the line down. */
    int32_t nx = y, ny = x;
    int32_t offset = ((z / LEVEL_MG_SCALE) * h +
                      ((z % LEVEL_MG_SCALE) * h) / LEVEL_MG_SCALE) / 20;
    Disp_SetDrawColor(&white);
    for(row=0;row<64;row++) {
        int32_t v0=nx*(-63)+ny*(row-31)+offset;
        int32_t v127=v0+nx*127;
        if(v0>=0 && v127>=0) {
            Disp_DrawLine(0u,(uint16_t)row,127u,(uint16_t)row);
        }
        else if(v0<0 && v127<0)continue;
        else if(nx!=0) {
            cross=(int16_t)(-v0/nx);
            if(nx>0) {
                first=(int16_t)(cross+1);
                if(first<0)first=0;
                if(first<128)
                    Disp_DrawLine((uint16_t)first,(uint16_t)row,
                                  127u,(uint16_t)row);
            }
            else {
                last=cross;
                if(last>127)last=127;
                if(last>=0)
                    Disp_DrawLine(0u,(uint16_t)row,(uint16_t)last,
                                  (uint16_t)row);
            }
        }
    }
    /* Contrast-inverted short marks, as in PB02B. */
    Disp_SetDrawColor(&xor_color);
    Disp_DrawLine(4u,32u,13u,32u);
    Disp_DrawLine(114u,32u,123u,32u);
}
/* Share the existing integer horizon geometry with magnetic calibration.
 * Clip window and footer are owned by the caller. */
void Motion_DrawHorizon(const int16_t level_mg[3]) {
    int32_t x = level_mg[0], y = level_mg[1];
    uint32_t h2 = (uint32_t)(x * x) + (uint32_t)(y * y);
    level_draw_horizon(x, y, (int32_t)isqrt_u32(h2), (int32_t)level_mg[2] * LEVEL_MG_SCALE);
}

/* A 36px diameter XOR circle must use a half-pixel center:
 * an integer center and x*x+y*y<18*18 would render only 35
 * pixels across. As in PB02B, store twice the circle center and
 * test squared distances in doubled-pixel coordinates. Offscreen
 * arcs remain clipped rather than disappearing abruptly. */
static void level_xor_disc(int16_t cx2,int16_t cy2,int16_t radius) {
    int16_t x,y,cx=(int16_t)(cx2/2),cy=(int16_t)(cy2/2);
    int16_t x0=(int16_t)(cx-radius-1),x1=(int16_t)(cx+radius+1);
    int16_t y0=(int16_t)(cy-radius-1),y1=(int16_t)(cy+radius+1);
    int32_t r2=(int32_t)(2*radius)*(2*radius);
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>127)x1=127;if(y1>63)y1=63;
    for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++) {
        int32_t dx=(int32_t)2*x-cx2,dy=(int32_t)2*y-cy2;
        if(dx*dx+dy*dy<r2)
            Disp_DrawPixel((uint16_t)x,(uint16_t)y);
    }
}
static int16_t level_round_pixel(int32_t value) {
    const int32_t half = LEVEL_PIXEL_SCALE / 2;
    return (int16_t)((value + (value < 0 ? -half : half)) / LEVEL_PIXEL_SCALE);
}

static void level_draw_zenith(int32_t x, int32_t y, int32_t h, int16_t angle) {
    int32_t dx = 0, dy = 0;
    uint8_t fg = 1u, xor_color = 2u;
    if (s_level_z > 0) {
        Disp_SetDrawColor(&fg);
        Disp_DrawBox(0u, 0u, 128u, 64u);
    }
    if (h != 0) {
        int32_t capped = angle > 4500 ? 4500 : angle;
        int32_t gap = (capped * (4 * LEVEL_RADIUS * LEVEL_PIXEL_SCALE) + 2250) / 4500;
        /* Preserve the perpendicular separation in doubled coordinates. */
        dx = -y * gap / h;
        dy = x * gap / h;
    }
    int32_t cx = 127 * LEVEL_PIXEL_SCALE, cy = 63 * LEVEL_PIXEL_SCALE;
    Disp_SetDrawColor(&xor_color);
    level_xor_disc(level_round_pixel(cx - dx), level_round_pixel(cy - dy), LEVEL_RADIUS);
    level_xor_disc(level_round_pixel(cx + dx), level_round_pixel(cy + dy), LEVEL_RADIUS);
}
/* Compact 3x3 hollow cross: top/left/right/bottom, center empty.
 * XOR each of the four distinct pixels once to avoid self-cancellation
 * (u8g2_DrawCircle(r=2) re-visits symmetric pixels). */
static void level_xor_degree_mark(void) {
    Disp_DrawPixel(124u,52u);
    Disp_DrawPixel(123u,53u);
    Disp_DrawPixel(125u,53u);
    Disp_DrawPixel(124u,54u);
}
static void level_angle_footer(int16_t angle) {
    char text[16];
    uint8_t xor_color=2u;
    uint8_t previous_font_mode=u8g2.font_decode.is_transparent;
    uint8_t previous_color=u8g2_GetDrawColor(&u8g2);
    uint16_t value,x,w;
    value = (uint16_t)level_abs(angle);
    if (value > 9999u) value = 9999u;
    (void)mini_snprintf(text,sizeof(text),"%2u.%02u",
                   (unsigned)(value/100u),(unsigned)(value%100u));
    Disp_SetFont(UI_FONT); /* preserve PB10A default font */
    w=Disp_GetUTF8Width(text);
    x=(uint16_t)(120u-(w>45u?45u:w));
    /* u8g2 defaults to SOLID text: background runs use color 0,
     * which erases a black rectangle even when foreground is XOR(2).
     * TRANSPARENT mode skips background runs and XORs glyph pixels
     * only, regardless of the underlying white/black horizon. */
    u8g2_SetFontMode(&u8g2,1u);
    Disp_SetDrawColor(&xor_color);
    Disp_DrawStr(x,62u,text);
    level_xor_degree_mark();
    /* Do not leak font transparency or XOR color into subsequent
     * MiaoUI screens; the following frame starts from its own state. */
    u8g2_SetFontMode(&u8g2,previous_font_mode);
    Disp_SetDrawColor(&previous_color);
}
void Motion_LevelerRun(ui_t *ui) {
    sc7a20_sample_t sample;
    int32_t x, y, z;
    int16_t angle;
    uint32_t now=Millis();
    if(!ui)return;
    if(!s_level_active) {
        s_level_active=1u;s_level_valid=0u;s_level_mode=0u;
        s_level_seq=0u;s_level_draw_at=0u;
        motion_tool_begin(1u);
    }
    if(ui->action!=UI_ACTION_NONE) {
        ui->action=UI_ACTION_BACK;
        Motion_LevelerOnLeave(ui);
        return;
    }
    if(s_level_draw_at && (uint32_t)(now-s_level_draw_at)<LEVEL_VIEW_MS)
        return;
    s_level_draw_at=now;
    draw_black();
    if(!SC7A20Algo_GetSample(&sample)) {
        uint8_t white=1u;
        Disp_SetDrawColor(&white);
        Disp_SetFont(UI_FONT);
        Disp_DrawStr(17u, 34u, I18N_Text("SC7A20 waiting"));
        Disp_SendBuffer();
        return;
    }
    if(sample.seq!=s_level_seq) {
        s_level_seq=sample.seq;
        /* Use the explicit drawing-space vector prepared by
         * SC7A20Algo; these are NOT the chip-native X/Y/Z axes.
         * PB10A Z- -> PB02B horizontal, X- -> PB02B left-white
         * vertical, Y- -> PB02B face-up dual zenith circle. */
        x = (int32_t)sample.level_mg[0] * LEVEL_MG_SCALE;
        y = (int32_t)sample.level_mg[1] * LEVEL_MG_SCALE;
        z = (int32_t)sample.level_mg[2] * LEVEL_MG_SCALE;
        if (!s_level_valid) {
            s_level_x = x;
            s_level_y = y;
            s_level_z = z;
            s_level_valid = 1u;
        } else {
            s_level_x += (x - s_level_x) / 5;
            s_level_y += (y - s_level_y) / 5;
            s_level_z += (z - s_level_z) / 5;
        }
    }
    x = s_level_x;
    y = s_level_y;
    z = s_level_z;
    /* Keep as much Q4 precision as possible while bounding x*x + y*y. */
    uint32_t range = level_abs(x) | level_abs(y) | level_abs(z);
    while (range > 32767u) {
        x /= 2;
        y /= 2;
        z /= 2;
        range >>= 1;
    }
    int32_t h = (int32_t)isqrt_u32((uint32_t)(x * x + y * y));
    int16_t plumb_cdeg = atan2_cdeg(h, (int32_t)level_abs(z));
    uint8_t roll_mode = (uint8_t)(level_abs(y) > level_abs(x));
    /* Original hysteresis: enter plumb below 43°, exit above 47°. */
    if (s_level_mode == 2u) {
        if (plumb_cdeg >= LEVEL_PLUMB_EXIT) s_level_mode = roll_mode;
    } else {
        s_level_mode = plumb_cdeg <= LEVEL_PLUMB_ENTER ? 2u : roll_mode;
    }
    if (s_level_mode == 2u) {
        angle = plumb_cdeg;
        level_draw_zenith(x, y, h, angle);
    } else {
        int32_t ax = (int32_t)level_abs(x), ay = (int32_t)level_abs(y);
        angle = s_level_mode == 0u ? atan2_cdeg(ay, ax) : atan2_cdeg(ax, ay);
        level_draw_horizon(x, y, h, s_level_z);
    }
    level_angle_footer(angle);
    Disp_SendBuffer();
}

/* Physical PB10A face-to-label correspondence (do not copy PB02B):
 * UP    -> Z- (normal flat placement, screen upright);
 * DOWN  -> Z+ (opposite placement);
 * RIGHT -> X- (right edge upwards);
 * LEFT  -> X+ (right edge downwards);
 * face UP   -> Y- (screen faces upwards);
 * face DOWN -> Y+ (screen faces downwards).
 * Face indices remain raw X-,X+,Y-,Y+,Z-,Z+ for the six-face matrix
 * calculation and retained .noinit calibration data. */
static uint8_t cal_detect(const int16_t raw[3]) {
    int16_t x=raw[0],y=raw[1],z=raw[2];
    int16_t ax=cal_abs(x),ay=cal_abs(y),az=cal_abs(z);
    if(ax>CAL_AXIS_MIN_ABS && ay<CAL_OTHER_MAX_ABS &&
       az<CAL_OTHER_MAX_ABS)return x<0?0u:1u;
    if(ay>CAL_AXIS_MIN_ABS && ax<CAL_OTHER_MAX_ABS &&
       az<CAL_OTHER_MAX_ABS)return y<0?2u:3u;
    if(az>CAL_AXIS_MIN_ABS && ax<CAL_OTHER_MAX_ABS &&
       ay<CAL_OTHER_MAX_ABS)return z<0?4u:5u;
    return CAL_FACE_NONE;
}
static void cal_reset_candidate(void) {
    s_cal_candidate = CAL_FACE_NONE;
    s_cal_capture.count = 0u;
}
static void cal_start(uint8_t face, const int values[3]) {
    s_cal_candidate = face;
    s_cal_capture.count = 0u;
    (void)axis_capture_add(&s_cal_capture, values, CAL_STABLE_RANGE);
}
static void cal_capture(uint8_t face) {
    uint8_t all_done = 1u;
    axis_capture_mean(&s_cal_capture, s_cal_faces[face]);
    s_cal_done[face] = 1u;
    cal_reset_candidate();
    for (unsigned i = 0u; i < CAL_FACE_COUNT; ++i)
        if (!s_cal_done[i])
            all_done = 0u;
    if (all_done) {
        s_cal_state = SC7A20Algo_ApplySixFace((const int16_t (*)[3])s_cal_faces) ? CAL_SAVED : CAL_FAILED;
        s_cal_complete_at = Millis();
    }
}
static void cal_process_sample(const sc7a20_sample_t *sample) {
    const int16_t *raw = sample->raw;
    uint8_t face = cal_detect(raw);
    if (face == CAL_FACE_NONE || s_cal_done[face]) {
        cal_reset_candidate();
        return;
    }
    const int values[3] = {raw[0], raw[1], raw[2]};
    if (face != s_cal_candidate || !axis_capture_add(&s_cal_capture, values, CAL_STABLE_RANGE)) {
        cal_start(face, values);
        return;
    }
    /* The first sample starts the original hold-interval counter at zero. */
    if (s_cal_capture.count > CAL_HOLD_INTERVALS)
        cal_capture(face);
}
static void cal_center(uint16_t y, const char *key) {
    Disp_DrawCentered(y, I18N_Text(key), 4u);
}

static void cal_check(uint16_t x, uint16_t y) {
    Disp_DrawLine(x - 4u, y, x - 1u, y + 3u);
    Disp_DrawLine(x - 1u, y + 3u, x + 5u, y - 3u);
}

static void cal_draw_face(unsigned face) {
    /* Same order as SC7A20Algo_ApplySixFace: X-, X+, Y-, Y+, Z-, Z+. */
    static const uint8_t centers[CAL_FACE_COUNT][2] = {
        {113, 34},
        {15, 34},
        {44, 34},
        {84, 34},
        {64, 22},
        {64, 46},
    };
    static const int8_t directions[CAL_FACE_COUNT][2] = {
        {1, 0},
        {-1, 0},
        {0, 0},
        {0, 0},
        {0, -1},
        {0, 1},
    };
    int x = centers[face][0], y = centers[face][1];
    uint8_t white = 1u, black = 0u;
    uint8_t side_view = face == 2u || face == 3u;
    if (face == s_cal_candidate) {
        unsigned half_width = side_view ? 12u : 7u;
        Disp_DrawBox(x - half_width, y - 7, half_width * 2u + 1u, 15u);
        Disp_SetDrawColor(&black);
    }
    if (side_view) {
        /* Side view: outline = housing, solid line = OLED, base = table. */
        Disp_DrawFrame(x - 9, y - 3, 19u, 7u);
        unsigned screen_y = face == 2u ? y - 4 : y + 4;
        Disp_DrawLine(x - 5, screen_y, x + 5, screen_y);
        Disp_DrawLine(x - 10, y + 6, x + 10, y + 6);
        if (s_cal_done[face])
            cal_check(x + 16, y);
    } else if (s_cal_done[face]) {
        cal_check(x, y);
    } else {
        int dx = directions[face][0], dy = directions[face][1];
        int tip_x = x + dx * 4, tip_y = y + dy * 4;
        int base_x = x + dx, base_y = y + dy;
        Disp_DrawLine(x - dx * 4, y - dy * 4, tip_x, tip_y);
        Disp_DrawLine(base_x - dy * 3, base_y + dx * 3, tip_x, tip_y);
        Disp_DrawLine(base_x + dy * 3, base_y - dx * 3, tip_x, tip_y);
    }
    Disp_SetDrawColor(&white);
}

static void cal_draw(void) {
    uint8_t white = 1u;
    draw_black();
    Disp_SetClipWindow(4u, 3u, 124u, 61u);
    Disp_SetDrawColor(&white);
    Disp_SetFont(UI_FONT);
    if (s_cal_state == CAL_INTRO) {
        cal_center(13u, "Accel Calibration");
        cal_center(27u, "Rest on each face");
        cal_center(41u, "Hold still for 2s");
        cal_center(59u, "CONF: Start");
    } else if (s_cal_state == CAL_SAVED) {
        cal_center(27u, "Calibration");
        cal_center(43u, "Cal saved");
    } else if (s_cal_state == CAL_FAILED) {
        cal_center(24u, "Cal error");
        cal_center(39u, "Check orientation");
        cal_center(58u, "Any key: Exit");
    } else {
        char info[8];
        unsigned count = 0u;
        for (unsigned face = 0u; face < CAL_FACE_COUNT; ++face) {
            count += s_cal_done[face] != 0u;
            cal_draw_face(face);
        }
        Disp_DrawStr(4u, 13u, I18N_Text(s_cal_candidate == CAL_FACE_NONE ? "Place on a face" : "Hold still"));
        mini_snprintf(info, sizeof(info), "%u/6", count);
        Disp_DrawStr(124u - Disp_GetUTF8Width(info), 13u, info);
        Disp_DrawFrame(8u, 55u, 112u, 6u);
        if (s_cal_capture.count > 1u)
            Disp_DrawBox(10u, 57u, 108u * (s_cal_capture.count - 1u) / CAL_HOLD_INTERVALS, 2u);
    }
    Disp_SetMaxClipWindow();
    Disp_SendBuffer();
}

void Motion_CalibrationRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_BACK)
        return;
    uint32_t now = Millis();
    if (s_cal_state == CAL_CLOSED) {
        s_cal_state = CAL_INTRO;
        pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
        pm_api_refresh_idle();
    }
    if (s_cal_state == CAL_INTRO) {
        if (ui->action == UI_ACTION_ENTER) {
            memset(s_cal_done, 0, sizeof(s_cal_done));
            s_cal_seq = s_cal_sample_at = 0u;
            cal_reset_candidate();
            SC7A20Algo_SetToolMode(2u);
            s_cal_state = CAL_CAPTURING;
        }
        ui->action = UI_ACTION_NONE;
    } else if (ui->action != UI_ACTION_NONE) {
        return;
    }

    if (s_cal_state == CAL_SAVED && (uint32_t)(now - s_cal_complete_at) >= 1200u) {
        ui->action = UI_ACTION_BACK;
        return;
    }
    if (s_cal_state == CAL_CAPTURING) {
        sc7a20_sample_t sample;
        if (SC7A20Algo_GetSample(&sample) && sample.seq != s_cal_seq &&
            (!s_cal_sample_at || (uint32_t)(now - s_cal_sample_at) >= CAL_VIEW_MS - 25u)) {
            s_cal_seq = sample.seq;
            s_cal_sample_at = now;
            cal_process_sample(&sample);
        }
    }
    cal_draw();
    UI_RequestNextFrame(ui, CAL_VIEW_MS);
}
