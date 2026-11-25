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
#include <string.h>

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

static uint8_t s_cal_active,s_cal_done[CAL_FACE_COUNT],s_cal_candidate;
static uint8_t s_cal_intervals,s_cal_samples,s_cal_completed;
static int16_t s_cal_faces[CAL_FACE_COUNT][3];
static int16_t s_cal_min[3],s_cal_max[3];
static int32_t s_cal_sum[3];
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
    if(!s_cal_active)return;
    s_cal_active=0u;
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

static void level_draw_horizon(int32_t x, int32_t y, int32_t h) {
    int16_t row, first, last, cross;
    uint8_t white = 1u, xor_color = 2u;
    if (h == 0) return;

    /* Multiply the original line equation by the XY magnitude. X and Y
     * use the scaled vector, Z is Q4 milligravity. Split Z at the mg
     * boundary so the product fits in 32 bits without losing its fraction.
     * Upright X < 0 is white above, positive Z moves the line down. */
    int32_t nx = y, ny = x;
    int32_t z = s_level_z;
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
        Disp_DrawStr(17u,34u,"SC7A20 waiting");
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
        level_draw_horizon(x, y, h);
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
 * Face indices remain raw X-,X+,Y-,Y+,Z-,Z+ for the six-axis slope
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
    s_cal_candidate=CAL_FACE_NONE;
    s_cal_intervals=0u;
    s_cal_samples=0u;
}
static void cal_start(uint8_t face,const int16_t raw[3]) {
    uint8_t i;
    s_cal_candidate=face;
    s_cal_intervals=0u;
    s_cal_samples=1u;
    for(i=0u;i<3u;i++) {
        s_cal_min[i]=raw[i];
        s_cal_max[i]=raw[i];
        s_cal_sum[i]=raw[i];
    }
}
static void cal_capture(uint8_t face) {
    uint8_t i,all_done=1u;
    for(i=0u;i<3u;i++)
        s_cal_faces[face][i]=(int16_t)(s_cal_sum[i]/s_cal_samples);
    s_cal_done[face]=1u;
    cal_reset_candidate();
    for(i=0u;i<CAL_FACE_COUNT;i++)
        if(!s_cal_done[i])all_done=0u;
    if(all_done) {
        s_cal_completed=SC7A20Algo_ApplySixFace(
            (const int16_t (*)[3])s_cal_faces)?1u:2u;
        s_cal_complete_at=Millis();
    }
}
static void cal_process_sample(const sc7a20_sample_t *sample) {
    uint8_t face,i;
    const int16_t *raw=sample->raw;
    if(s_cal_completed)return;
    face=cal_detect(raw);
    if(face==CAL_FACE_NONE ||
       s_cal_done[face]) {cal_reset_candidate();return;}
    if(face!=s_cal_candidate) {cal_start(face,raw);return;}
    for(i=0u;i<3u;i++) {
        if(raw[i]<s_cal_min[i])s_cal_min[i]=raw[i];
        if(raw[i]>s_cal_max[i])s_cal_max[i]=raw[i];
        if((int32_t)s_cal_max[i]-s_cal_min[i]>CAL_STABLE_RANGE) {
            cal_start(face,raw);
            return;
        }
    }
    for(i=0u;i<3u;i++)s_cal_sum[i]+=raw[i];
    ++s_cal_samples;
    if(++s_cal_intervals>=CAL_HOLD_INTERVALS)cal_capture(face);
}
static void cal_draw(void) {
    char info[28];
    uint8_t count=0u,i,white=1u;
    draw_black();
    Disp_SetDrawColor(&white);
    Disp_SetFont(UI_FONT);
    for(i=0u;i<CAL_FACE_COUNT;i++)count+=s_cal_done[i]!=0u;
    if(s_cal_completed==1u) {
        Disp_DrawStr(28u,28u,"Calibration");
        Disp_DrawStr(41u,43u,"Saved");
    }
    else if(s_cal_completed==2u) {
        Disp_DrawStr(32u,29u,"Cal error");
        Disp_DrawStr(12u,44u,"Check orientation");
    }
    else {
        (void)mini_snprintf(info,sizeof(info),"CAL %u/6",(unsigned)count);
        Disp_DrawStr(3u,10u,info);
        if(s_cal_candidate!=CAL_FACE_NONE)
            (void)mini_snprintf(info,sizeof(info),"%2u/10",
                           (unsigned)s_cal_intervals);
        else (void)mini_snprintf(info,sizeof(info),"Hold 2s");
        Disp_DrawStr(77u,10u,info);
        Disp_DrawStr(54u,22u,s_cal_done[4]?"OK":"UP");
        Disp_DrawStr(48u,61u,s_cal_done[5]?"OK":"DOWN");
        Disp_DrawStr(1u,36u,s_cal_done[1]?"OK":"LEFT");
        Disp_DrawStr(94u,36u,s_cal_done[0]?"OK":"RIGHT");
        (void)mini_snprintf(info,sizeof(info),"ZU:%s ZD:%s",
                       s_cal_done[2]?"OK":"--",
                       s_cal_done[3]?"OK":"--");
        Disp_DrawStr(26u,47u,info);
    }
    Disp_SendBuffer();
}
void Motion_CalibrationRun(ui_t *ui) {
    sc7a20_sample_t sample;
    uint32_t now=Millis();
    if(!ui)return;
    if(!s_cal_active) {
        s_cal_active=1u;
        memset(s_cal_done,0,sizeof(s_cal_done));
        s_cal_completed=0u;s_cal_seq=0u;s_cal_sample_at=0u;
        cal_reset_candidate();
        motion_tool_begin(2u);
    }
    if(ui->action!=UI_ACTION_NONE) {
        ui->action=UI_ACTION_BACK;
        Motion_CalibrationOnLeave(ui);
        return;
    }
    if(s_cal_completed==1u &&
       (uint32_t)(now-s_cal_complete_at)>1200u) {
        Motion_CalibrationOnLeave(ui);
        ui->action=UI_ACTION_BACK;
        return;
    }
    if(!s_cal_completed && SC7A20Algo_GetSample(&sample) &&
       sample.seq!=s_cal_seq &&
       (!s_cal_sample_at || (uint32_t)(now-s_cal_sample_at)>=CAL_VIEW_MS-25u)) {
        s_cal_seq=sample.seq;
        s_cal_sample_at=now;
        cal_process_sample(&sample);
    }
    cal_draw();
}
