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
#include <math.h>
#include <stdio.h>
#include <string.h>

#define MOTION_APP_PM_BLOCK 0x20u
#define LEVEL_VIEW_MS       40u
#define LEVEL_PLUMB_ENTER   43.0f
#define LEVEL_PLUMB_EXIT    47.0f
#define RAD_TO_DEG          57.2957795f
#define LEVEL_RADIUS        12
#define CAL_VIEW_MS         200u
#define CAL_HOLD_INTERVALS  10u
#define CAL_AXIS_MIN_ABS    700
#define CAL_OTHER_MAX_ABS   200
#define CAL_STABLE_RANGE    100
#define CAL_FACE_COUNT      6u
#define CAL_FACE_NONE       0xFFu

static uint8_t s_level_active,s_level_valid,s_level_mode;
static uint32_t s_level_seq,s_level_draw_at;
static float s_level_x,s_level_y,s_level_z;

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
    pm_api_set_sleep_block(MOTION_APP_PM_BLOCK,1u);
    pm_api_refresh_idle();
    SC7A20Algo_SetToolMode(mode);
    Disp_SetRotation(0u); /* native layout from the very first tool frame */
}
static void motion_tool_end(void) {
    SC7A20Algo_SetToolMode(0u);
    pm_api_set_sleep_block(MOTION_APP_PM_BLOCK,0u);
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
static void level_draw_horizon(float x,float y,float z) {
    int32_t nx,ny,offset;
    int16_t row,first,last,cross;
    float h=sqrtf(x*x+y*y);
    uint8_t white=1u,xor_color=2u;
    if(h<0.0001f)return;
    /* Original plane: f=(-Y/h)(x-cx)+(X/h)(y-cy)+z*offset.
     * Use Q10 coefficients and scanline spans: 64 lines, no 8 KB canvas
     * and no per-pixel floating-point loop on the Cortex-M0+. */
    nx=(int32_t)((-y/h)*1024.0f);
    ny=(int32_t)(( x/h)*1024.0f);
    offset=(int32_t)(z*50.0f*1024.0f);
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
/* Draw a clipped XOR disc even when its center is off-screen.
 * In PB02B the circles can move into the screen from outside. */
static void level_xor_disc(int16_t cx,int16_t cy,int16_t radius) {
    int16_t x,y,x0=(int16_t)(cx-radius),x1=(int16_t)(cx+radius);
    int16_t y0=(int16_t)(cy-radius),y1=(int16_t)(cy+radius);
    const int32_t r2=(int32_t)radius*radius;
    if(x0<0)x0=0;if(y0<0)y0=0;
    if(x1>127)x1=127;if(y1>63)y1=63;
    for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++) {
        int32_t dx=x-cx,dy=y-cy;
        if(dx*dx+dy*dy<r2)
            Disp_DrawPixel((uint16_t)x,(uint16_t)y);
    }
}
static void level_draw_zenith(float x,float y,float z,float angle) {
    float h=sqrtf(x*x+y*y);
    float gap=angle*(4.0f*LEVEL_RADIUS)/45.0f;
    float dx=0.0f,dy=0.0f;
    int16_t x1,y1,x2,y2;
    uint8_t fg=1u,xor_color=2u;
    if(gap>4.0f*LEVEL_RADIUS)gap=4.0f*LEVEL_RADIUS;
    if(z>0.0f) {
        Disp_SetDrawColor(&fg);
        Disp_DrawBox(0u,0u,128u,64u);
    }
    if(h>0.0001f) {
        /* The two circles separate perpendicular to XY gravity,
         * rather than along the gravity direction itself. */
        dx=-y*gap/h*0.5f;
        dy= x*gap/h*0.5f;
    }
    x1=(int16_t)(63.5f-dx);
    y1=(int16_t)(31.5f-dy);
    x2=(int16_t)(63.5f+dx);
    y2=(int16_t)(31.5f+dy);
    Disp_SetDrawColor(&xor_color);
    level_xor_disc(x1,y1,LEVEL_RADIUS);
    level_xor_disc(x2,y2,LEVEL_RADIUS);
    /* Keep the zenith reference visible when the XOR discs overlap. */
    Disp_DrawCircle(63u,31u,LEVEL_RADIUS,U8G2_DRAW_ALL);
}
static void level_angle_footer(float angle) {
    char text[16];
    uint8_t black=0u,white=1u;
    uint16_t value,x,w;
    if(angle<0.0f)angle=-angle;
    if(angle>99.99f)angle=99.99f;
    value=(uint16_t)(angle*100.0f+0.5f);
    (void)snprintf(text,sizeof(text),"%2u.%02u",
                   (unsigned)(value/100u),(unsigned)(value%100u));
    Disp_SetFont(UI_FONT); /* project's default font, not source UNSCII */
    w=Disp_GetUTF8Width(text);
    x=(uint16_t)(120u-(w>45u?45u:w));
    /* Opaque label only in bottom-right corner; full-width horizon is
     * left unobstructed elsewhere, with no right-hand XYZ columns. */
    Disp_SetDrawColor(&black);
    Disp_DrawBox(x>4u?x-4u:0u,49u,(uint16_t)(128u-(x>4u?x-4u:0u)),15u);
    Disp_SetDrawColor(&white);
    Disp_DrawStr(x,62u,text);
    Disp_DrawCircle(124u,53u,2u,U8G2_DRAW_ALL);
}
void Motion_LevelerRun(ui_t *ui) {
    sc7a20_sample_t sample;
    float x,y,z,h,angle,plumb_deg;
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
        x=sample.mg[0]*0.001f;
        y=sample.mg[1]*0.001f;
        z=sample.mg[2]*0.001f;
        if(!s_level_valid) {
            s_level_x=x;s_level_y=y;s_level_z=z;
            s_level_valid=1u;
        }
        else {
            s_level_x+=0.2f*(x-s_level_x);
            s_level_y+=0.2f*(y-s_level_y);
            s_level_z+=0.2f*(z-s_level_z);
        }
    }
    x=s_level_x;y=s_level_y;z=s_level_z;
    h=sqrtf(x*x+y*y);
    plumb_deg=atan2f(h,fabsf(z))*RAD_TO_DEG;
    /* Original hysteresis: enter plumb below 43°, exit above 47°. */
    if(s_level_mode==2u) {
        if(plumb_deg>=LEVEL_PLUMB_EXIT)
            s_level_mode=fabsf(y)>fabsf(x)?1u:0u;
    }
    else if(plumb_deg<=LEVEL_PLUMB_ENTER)s_level_mode=2u;
    else s_level_mode=fabsf(y)>fabsf(x)?1u:0u;
    if(s_level_mode==2u) {
        angle=plumb_deg;
        level_draw_zenith(x,y,z,plumb_deg);
    }
    else {
        angle=(s_level_mode==0u?
            atan2f(fabsf(y),fabsf(x)):
            atan2f(fabsf(x),fabsf(y)))*RAD_TO_DEG;
        level_draw_horizon(x,y,z);
    }
    level_angle_footer(angle);
    Disp_SendBuffer();
}

/* Six stable faces directly correspond to the PB02B six-face procedure:
 * X- screen top up, X+ bottom up, Y- right edge up, Y+ left edge up,
 * Z- screen face up and Z+ screen face down. */
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
        (void)snprintf(info,sizeof(info),"CAL %u/6",(unsigned)count);
        Disp_DrawStr(3u,10u,info);
        if(s_cal_candidate!=CAL_FACE_NONE)
            (void)snprintf(info,sizeof(info),"%2u/10",
                           (unsigned)s_cal_intervals);
        else (void)snprintf(info,sizeof(info),"Hold 2s");
        Disp_DrawStr(77u,10u,info);
        Disp_DrawStr(54u,22u,s_cal_done[0]?"OK":"UP");
        Disp_DrawStr(48u,61u,s_cal_done[1]?"OK":"DOWN");
        Disp_DrawStr(1u,36u,s_cal_done[3]?"OK":"LEFT");
        Disp_DrawStr(94u,36u,s_cal_done[2]?"OK":"RIGHT");
        (void)snprintf(info,sizeof(info),"ZU:%s ZD:%s",
                       s_cal_done[4]?"OK":"--",
                       s_cal_done[5]?"OK":"--");
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
