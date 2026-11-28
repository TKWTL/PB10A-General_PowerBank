/* SC7A20: IRQ-driven I2C initialization, XYZ sampling and STOP1 policy. */
#include "sc7a20_algo.h"
#include "mini_crc.h"
#include "sc7a20.h"
#include "sensor_common.h"
#include "pt.h"
#include "main.h"
#include "pm_api.h"
#include <string.h>
#define SC_SAMPLE_MS 200u
#define SC_LEVEL_SAMPLE_MS 40u
#define SC_CAL_RECORD_TAG 0xA722u /* New affine six-face format. */
#define SC_CAL_Q12_ONE 4096
#define SC_CAL_DIAG_MIN 1024
#define SC_CAL_DIAG_MAX 12288
#define SC_CAL_CROSS_MAX 8192
#define SC_RETRY_MS 2000u
#define SC_ORIENT_ENTER_MG 650
#define SC_ORIENT_CONFIRM 10u /* 10 stable updates at 5 Hz ~2 seconds */
#define SC_MOTION_THS_REG 48u /* 768 mg at +/-2g, from PB02B */
#define SC_MOTION_DUR_REG 2u  /* 80 ms at 25 Hz */
#define SC_PICKUP_THS_REG 32u /* PB02B 6D threshold, 512 mg at +/-2g */
#define SC_PICKUP_DUR_REG 1u
/* PB02B Pickup Wake: all six +/-XYZ 6D orientation events, no HPF. */
#define SC_6D_ALL (SC7A20_INTMODE_6D_MOTION | SC7A20_INT_CFG_XHIE | \
    SC7A20_INT_CFG_XLIE | SC7A20_INT_CFG_YHIE | SC7A20_INT_CFG_YLIE | \
    SC7A20_INT_CFG_ZHIE | SC7A20_INT_CFG_ZLIE)
#define SC_MOTION_XYZ (SC7A20_INTMODE_OR | SC7A20_INT_CFG_XHIE | \
    SC7A20_INT_CFG_YHIE | SC7A20_INT_CFG_ZHIE)
enum {SC_PM_IDLE=0u,SC_PM_SUSPEND,SC_PM_RESUME};

#define SC_CTRL1_RUN (SC7A20_DEFAULT_ODR | SC7A20_DEFAULT_AXIS_EN)
#define SC_CTRL4_RUN ((SC7A20_DEFAULT_BDU ? SC7A20_CTRL4_BDU : 0u) | SC7A20_DEFAULT_FULLSCALE | (SC7A20_DEFAULT_HIGHRES ? SC7A20_CTRL4_HR : 0u))

static uint8_t s_stage,s_waiting,s_paused,s_suspended,s_pm_waiting,s_pm_mode,s_pm_step,s_pm_routes;
static volatile uint8_t s_wake_armed;
static uint8_t s_pickup_wake,s_motion_wake;
static uint8_t s_orientation,s_candidate,s_confirm;
static uint32_t s_pm_deadline;
static uint8_t s_buffer[6];
static struct pt s_transfer_pt;
static volatile uint8_t s_flag,s_pm_flag;
static uint32_t s_last_sample,s_retry_at;
static int s_axes[3];
static uint8_t s_tool_mode, s_orient_div;
static sc7a20_sample_t s_sample;

/* Preserve calibration across software / NRST / watchdog resets while SRAM
 * remains powered. Linked into RW_NOINIT (the last 256 bytes of 36KB SRAM)
 * alongside g_fault_record, but never initialized by the C startup.
 * NO VBAT: neither RAM nor RTC backup survives full loss of VDD.
 * No initializer: magic + CRC + range guards decide whether RAM is usable. */
/* 24-byte payload: raw-code offset and enclosure-aligned 3x3 Q12
 * transform. The old diagonal-only record cannot recover cross-axis terms. */
typedef union {
    struct {
        int16_t zero[3];
        int16_t matrix[3][3];
    } data;
    uint32_t words[6];
} sc_cal_payload_t;
typedef struct {
    sc_cal_payload_t payload;
    uint32_t crc;
    uint32_t tag; /* Version/magic; commit LAST */
} sc_cal_noinit_t;
_Static_assert(sizeof(sc_cal_noinit_t) == 32u, "SC7A20 retention layout");
static volatile sc_cal_noinit_t s_calibration_noinit
    __attribute__((section(".bss.noinit"), used, aligned(4)));
static uint8_t s_calibrated;
static ui_item_t s_items[3],s_wake_items[2];
static ui_data_t s_data[3],s_wake_data[2];
static ui_element_t s_elements[3],s_wake_elements[2];

static uint8_t sc_transfer(uint8_t read, uint8_t reg, uint8_t count) {
    s_waiting = (uint8_t)PT_SCHEDULE(SC7A20_TransferPT(&s_transfer_pt, read, reg, s_buffer, count, &s_flag));
    return s_waiting ? 0u : s_flag;
}

static void sc_offline(void) {
    s_orientation=SC7A20_ORIENT_UNKNOWN;
    s_candidate=SC7A20_ORIENT_UNKNOWN;s_confirm=0u;
    s_stage=0u;
    s_retry_at=Millis()+SC_RETRY_MS;
    s_sample.seq=0u;
    SC7A20_AsyncSetReady(0u);
}

/* PB10A SC7A20 measured mounting direction, from the user's original
 * physical test (not inferred from the PB02B board layout):
 *
 * "系统是扁方盒，屏幕在侧面；
 *  平放且屏幕正向时 Z 轴约 -1g；
 *  屏幕面朝下时 Y 轴约 +1g；
 *  屏幕右侧朝下时 X 轴约 +1g。"
 *
 * Opposite orientations invert the same native axis:
 *   Z+ = upside-down from the normal flat Z- reference;
 *   Y- = display face upwards (Y+ = face downwards);
 *   X- = display RIGHT edge upwards (X+ = right edge downwards).
 *
 * This differs from the PB02B leveler source, which explicitly expects:
 *   Leveler X=-1g -> horizontal horizon, top white/bottom black;
 *   Leveler Y=+1g -> vertical horizon, left white/right black;
 *   Leveler Z=-1g -> face-up zenith, black ground/white rings.
 *
 * Therefore ONLY the drawing-space vector is remapped:
 *   level_X = PB10A Z, level_Y = -PB10A X, level_Z = PB10A Y.
 * No hardware XYZ swapping and no change to raw six-face calibration
 * order or screen-auto-rotation Z threshold. */
static void sc_level_axes_update(void) {
    s_sample.level_mg[0]=s_sample.mg[2];
    s_sample.level_mg[1]=(int16_t)-s_sample.mg[0];
    s_sample.level_mg[2]=s_sample.mg[1];
}

/* Screen auto-rotation remains based on native Z=-1g NORMAL and
 * Z=+1g FLIPPED. When face-down (Y+) or right-edge-down (X+),
 * neither Z threshold is crossed, so retain the previous orientation. */
static void sc_orientation_update(int z_mg) {
    uint8_t next;
    if(z_mg<=-SC_ORIENT_ENTER_MG)next=SC7A20_ORIENT_NORMAL;
    else if(z_mg>=SC_ORIENT_ENTER_MG)next=SC7A20_ORIENT_FLIPPED;
    else {s_candidate=SC7A20_ORIENT_UNKNOWN;s_confirm=0u;return;}
    if(next==s_orientation) {
        s_candidate=SC7A20_ORIENT_UNKNOWN;s_confirm=0u;return;
    }
    if(next!=s_candidate) {s_candidate=next;s_confirm=0u;return;}
    if(++s_confirm>=SC_ORIENT_CONFIRM) {
        s_orientation=next;
        s_candidate=SC7A20_ORIENT_UNKNOWN;s_confirm=0u;
    }
}

uint8_t SC7A20Algo_GetRotation(uint8_t *flipped) {
    if(s_orientation==SC7A20_ORIENT_UNKNOWN)return 0u;
    if(flipped)*flipped=(uint8_t)(s_orientation==SC7A20_ORIENT_FLIPPED);
    return 1u;
}

static uint32_t sc_calibration_crc(const sc_cal_payload_t *p) {
    /* Reuse the existing three-word CRC primitive for both payload halves. */
    uint32_t a = crc32_words3(p->words[0], p->words[1], p->words[2]);
    uint32_t b = crc32_words3(p->words[3], p->words[4], p->words[5]);
    return crc32_words3(a, b, SC_CAL_RECORD_TAG);
}

static void sc_calibration_load(void) {
    s_calibrated = 0u;
    if (s_calibration_noinit.tag != SC_CAL_RECORD_TAG)
        return;
    sc_cal_payload_t p = s_calibration_noinit.payload;
    if (s_calibration_noinit.crc != sc_calibration_crc(&p))
        return;
    for (unsigned i = 0u; i < 3u; ++i) {
        if (p.data.zero[i] < -2048 || p.data.zero[i] > 2047 ||
            p.data.matrix[i][i] < SC_CAL_DIAG_MIN || p.data.matrix[i][i] > SC_CAL_DIAG_MAX)
            return;
        for (unsigned j = 0u; j < 3u; ++j) {
            if (i != j && (p.data.matrix[i][j] < -SC_CAL_CROSS_MAX ||
                           p.data.matrix[i][j] > SC_CAL_CROSS_MAX))
                return;
        }
    }
    s_calibrated = 1u;
}

/* Symmetric round-to-nearest with positive denominator. */
static int32_t sc_round_div(int32_t num, int32_t den) {
    return (num + (num < 0 ? -den / 2 : den / 2)) / den;
}

uint8_t SC7A20Algo_ApplySixFace(const int16_t face_raw[6][3]) {
    sc_cal_payload_t p;
    int32_t a[3][6] = {{0}};
    if (!face_raw)
        return 0u;
    /* For raw row i and physical axis j, D[i][j] = raw(+j)-raw(-j).
     * Six-face mean estimates the offset, and M = 2000*inverse(D)
     * converts raw deltas into enclosure-aligned milligravity. */
    for (unsigned i = 0u; i < 3u; ++i) {
        int32_t sum = 0;
        for (unsigned j = 0u; j < 3u; ++j) {
            int32_t neg = face_raw[2u * j][i], pos = face_raw[2u * j + 1u][i];
            if (neg < -2048 || neg > 2047 || pos < -2048 || pos > 2047)
                return 0u;
            int32_t span = pos - neg;
            if (i == j ? (span < 1000 || span > 4000) : (span < -400 || span > 400))
                return 0u;
            a[i][j] = span;
            sum += pos + neg;
        }
        p.data.zero[i] = (int16_t)sc_round_div(sum, 6);
        a[i][i + 3u] = 2000;
    }

    /* Gauss-Jordan on [D | 2000 I]. D is strictly diagonally dominant
     * after the six-face checks, so no pivot swaps or 64-bit math needed.
     * Normalized rows use Q12; unnormalized rows keep raw-code units. */
    for (unsigned k = 0u; k < 3u; ++k) {
        int32_t pivot = a[k][k];
        if (pivot < 500)
            return 0u;
        for (unsigned j = 0u; j < 6u; ++j)
            a[k][j] = sc_round_div(a[k][j] * SC_CAL_Q12_ONE, pivot);
        for (unsigned i = 0u; i < 3u; ++i) {
            if (i == k)
                continue;
            int32_t factor = a[i][k];
            for (unsigned j = 0u; j < 6u; ++j)
                a[i][j] -= sc_round_div(factor * a[k][j], SC_CAL_Q12_ONE);
        }
    }
    for (unsigned i = 0u; i < 3u; ++i) {
        if (a[i][i + 3u] < SC_CAL_DIAG_MIN || a[i][i + 3u] > SC_CAL_DIAG_MAX)
            return 0u;
        for (unsigned j = 0u; j < 3u; ++j) {
            int32_t v = a[i][j + 3u];
            if (i != j && (v < -SC_CAL_CROSS_MAX || v > SC_CAL_CROSS_MAX))
                return 0u;
            p.data.matrix[i][j] = (int16_t)v;
        }
    }

    s_calibration_noinit.tag = 0u;
    __DMB();
    s_calibration_noinit.payload = p;
    s_calibration_noinit.crc = sc_calibration_crc(&p);
    __DMB();
    s_calibration_noinit.tag = SC_CAL_RECORD_TAG;
    __DSB();
    s_calibrated = 1u;
    s_orientation = SC7A20_ORIENT_UNKNOWN;
    s_candidate = SC7A20_ORIENT_UNKNOWN;
    s_confirm = 0u;
    s_sample.seq = 0u;
    return 1u;
}

static void sc_calibrated_axes_update(void) {
    /* PB10A always samples +/-2g HR (1 mg/raw code). Keep the BSP's
     * per-axis calibration API unchanged for other projects. */
    if (!s_calibrated) {
        memcpy(s_sample.mg, s_sample.raw, sizeof(s_sample.mg));
        return;
    }
    int32_t delta[3];
    for (unsigned i = 0u; i < 3u; ++i)
        delta[i] = (int32_t)s_sample.raw[i] - s_calibration_noinit.payload.data.zero[i];
    /* The coefficient bounds keep both the dot product and the resulting
     * signed 16-bit mg value in range even at full-scale raw input. */
    for (unsigned i = 0u; i < 3u; ++i) {
        int32_t sum = 0;
        for (unsigned j = 0u; j < 3u; ++j)
            sum += delta[j] * s_calibration_noinit.payload.data.matrix[i][j];
        s_sample.mg[i] = (int16_t)sc_round_div(sum, SC_CAL_Q12_ONE);
    }
}

void SC7A20Algo_SetToolMode(uint8_t mode) {
    if (mode > 3u)
        mode = 0u;
    s_tool_mode=mode;
    s_orient_div=0u;
    s_sample.seq=0u; /* old sample must not be interpreted as new */
}
uint8_t SC7A20Algo_GetSample(sc7a20_sample_t *out) {
    if (!out || s_stage != 6u || s_suspended || s_paused ||
        s_sample.seq == 0u || (uint32_t)(Millis() - s_sample.at_ms) > 800u)
        return 0u;
    *out=s_sample; /* same foreground coroOS, no ISR writer */
    return 1u;
}

/* Called from EXTI5 ISR: never use I2C or touch MiaoUI in an interrupt. */
uint8_t SC7A20Algo_WakeIRQ(void) {return s_wake_armed;}

__attribute__((noinline)) void SC7A20Algo_Poll(void) {
    uint8_t result;
    if(s_suspended || (s_paused && !s_waiting))return;
    if(!s_waiting && s_stage==0u && (int32_t)(Millis()-s_retry_at)<0)return;
    switch(s_stage) {
    case 0u:
        result=sc_transfer(1u,SC7A20_STRG_WHO_AM_I,1u);
        if(!result)return;
        if(result!=1u || s_buffer[0]!=SC7A20_WHO_AM_I_VALUE){sc_offline();return;}
        s_stage=1u;break;
    case 1u:
        result=sc_transfer(1u,SC7A20_CTRG_CTRL6,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_buffer[0]|=SC7A20_CTRL6_H_LACTIVE;
        s_stage=2u;break;
    case 2u:
        if(!s_waiting)s_buffer[0]|=SC7A20_CTRL6_H_LACTIVE;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL6,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=3u;break;
    case 3u:
        if(!s_waiting)s_buffer[0]=SC_CTRL4_RUN;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL4,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=4u;break;
    case 4u:
        if(!s_waiting)s_buffer[0]=0xC0u; /* existing LIS2DH-compatible temperature setup */
        result=sc_transfer(0u,SC7A20_CTRG_TEMP_CFG,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        s_stage=5u;break;
    case 5u:
        if(!s_waiting)s_buffer[0]=SC_CTRL1_RUN;
        result=sc_transfer(0u,SC7A20_CTRG_CTRL1,1u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        SC7A20_AsyncSetReady(1u);
        s_last_sample=Millis()-SC_SAMPLE_MS;
        s_stage=6u;break;
    case 6u:
        if (!s_waiting && (uint32_t)(Millis() - s_last_sample) <
                              ((s_tool_mode == 1u || s_tool_mode == 3u) ? SC_LEVEL_SAMPLE_MS : SC_SAMPLE_MS))
            return;
        result=sc_transfer(1u,SC7A20_STRG_OUT_AUTO,6u);
        if(!result)return;
        if(result!=1u){sc_offline();return;}
        SC7A20_AsyncSetAxes(s_buffer);
        /* HR at 2g: one right-aligned code is one mg. */
        {int16_t x,y,z;
            SC7A20_ReadRawCode(&x,&y,&z);
            s_axes[0]=(int)x;s_axes[1]=(int)y;s_axes[2]=(int)z;
            s_sample.raw[0]=x;s_sample.raw[1]=y;s_sample.raw[2]=z;
            sc_calibrated_axes_update();
            sc_level_axes_update(); /* PB10A -> PB02B drawing axes */
            ++s_sample.seq;if(!s_sample.seq)++s_sample.seq;
            s_sample.at_ms = Millis();
            /* 25Hz tool sampling must not speed up the 5Hz
             * 2-second orientation hysteresis from the normal mode. */
            if ((s_tool_mode != 1u && s_tool_mode != 3u) || ++s_orient_div >= 5u) {
                s_orient_div=0u;
                sc_orientation_update(s_sample.mg[2]);
            }
        }
        s_last_sample=Millis();break;
    default:sc_offline();break;
    }
}

/* All PM register accesses complete through the common IRQ-backed I2C1.
 * A PENDING result yields the PM thread, not the whole coroOS scheduler. */
static pm_hook_result_t sc_pm_xfer(uint8_t read,uint8_t reg,uint8_t value) {
    if(!s_pm_waiting) {
        if(!read)s_buffer[0]=value;
        if(!(read?SC7A20_AsyncRead(reg,s_buffer,1u,(uint8_t *)&s_pm_flag):
            SC7A20_AsyncWrite(reg,s_buffer,1u,(uint8_t *)&s_pm_flag)))
            return PM_HOOK_PENDING;
        s_pm_waiting=1u;
        return PM_HOOK_PENDING;
    }
    if(s_pm_flag==I2C_ASYNC_PENDING)return PM_HOOK_PENDING;
    s_pm_waiting=0u;
    return s_pm_flag==I2C_ASYNC_DONE?PM_HOOK_DONE:PM_HOOK_ERROR;
}

static pm_hook_result_t sc_prepare(void *ctx) {
    (void)ctx;
    s_paused=1u;
    return s_waiting?PM_HOOK_PENDING:PM_HOOK_DONE;
}

static pm_hook_result_t sc_suspend(void *ctx) {
    pm_hook_result_t result;
    uint8_t reg=0u,value=0u,read=0u;
    (void)ctx;
    if(s_stage!=6u && s_pm_mode==SC_PM_IDLE)return PM_HOOK_DONE;
    if(s_pm_mode!=SC_PM_SUSPEND) {
        s_pm_mode=SC_PM_SUSPEND;s_pm_step=0u;
        s_pm_routes=(uint8_t)((s_motion_wake?SC7A20_CTRL3_I1_AOI1:0u) |
            (s_pickup_wake?SC7A20_CTRL3_I1_AOI2:0u));
        s_wake_armed=0u;
    }
    for(;;) {
        if(s_pm_step==20u) {
            s_suspended=1u;
            s_pm_mode=SC_PM_IDLE;
            s_wake_armed=(uint8_t)(s_pm_routes!=0u);
            /* An event already asserted before arming must not be lost. */
            if(s_wake_armed && !LL_GPIO_IsInputPinSet(INT_GPIO_Port,INT_Pin)) {
                pm_api_on_exti_event(PM_WAKE_SC7A20);
                return PM_HOOK_PENDING; /* abort entry instead of losing a low level */
            }
            return PM_HOOK_DONE;
        }
        if(s_pm_step==13u || s_pm_step==15u) {
            if((int32_t)(Millis()-s_pm_deadline)<0)return PM_HOOK_PENDING;
            ++s_pm_step;
            continue;
        }
        switch(s_pm_step) {
        case 0u:reg=SC7A20_CTRG_CTRL3;value=0u;break; /* mask IRQ routes first */
        case 21u:reg=SC7A20_CTRG_TEMP_CFG;value=0u;break;
        case 1u:reg=SC7A20_CTRG_INT1_CFG;value=0u;break;
        case 2u:reg=SC7A20_CTRG_INT2_CFG;value=0u;break;
        case 3u:reg=SC7A20_CTRG_CTRL2;value=s_motion_wake?SC7A20_CTRL2_HPIS1:0u;break;
        case 4u:reg=SC7A20_CTRG_CTRL5;value=s_motion_wake?SC7A20_CTRL5_LIR_INT1:0u;break;
        case 5u:reg=SC7A20_CTRG_CTRL4;value=(uint8_t)(SC_CTRL4_RUN & ~SC7A20_CTRL4_HR);break;
        case 6u:reg=SC7A20_CTRG_CTRL6;value=SC7A20_CTRL6_H_LACTIVE;break;
        case 7u:reg=SC7A20_CTRG_INT1_THS;value=SC_MOTION_THS_REG;break;
        case 8u:reg=SC7A20_CTRG_INT1_DURATION;value=SC_MOTION_DUR_REG;break;
        case 9u:reg=SC7A20_CTRG_INT2_THS;value=SC_PICKUP_THS_REG;break;
        case 10u:reg=SC7A20_CTRG_INT2_DURATION;value=SC_PICKUP_DUR_REG;break;
        case 11u:reg=SC7A20_CTRG_INT2_CFG;value=s_pickup_wake?SC_6D_ALL:0u;break;
        case 12u:reg=SC7A20_CTRG_CTRL1;
            value=s_pm_routes?(SC_CTRL1_RUN | SC7A20_CTRL1_LPEN):
                (uint8_t)(SC_CTRL1_RUN & ~SC7A20_CTRL1_ODR_MSK);
            break;
        case 14u:reg=SC7A20_CTRG_REFERENCE;read=s_motion_wake;break;
        case 16u:reg=SC7A20_STRG_INT1_SOURCE;read=1u;break;
        case 17u:reg=SC7A20_STRG_INT2_SOURCE;read=1u;break;
        case 18u:reg=SC7A20_CTRG_INT1_CFG;value=s_motion_wake?SC_MOTION_XYZ:0u;break;
        case 19u:reg=SC7A20_CTRG_CTRL3;value=s_pm_routes;break;
        default:return PM_HOOK_ERROR;
        }
        if(s_pm_step==14u && !s_motion_wake)result=PM_HOOK_DONE;
        else result=sc_pm_xfer(read,reg,value);
        if(result!=PM_HOOK_DONE)return result;
        if(s_pm_step==0u)s_pm_step=21u;
        else if(s_pm_step==21u)s_pm_step=s_pm_routes?1u:12u;
        else if(s_pm_step==12u && !s_pm_routes)s_pm_step=20u;
        else {
            ++s_pm_step;
            if(s_pm_step==13u)s_pm_deadline=Millis()+420u;
            if(s_pm_step==15u)s_pm_deadline=Millis()+120u;
        }
    }
}

static pm_hook_result_t sc_resume(void *ctx) {
    pm_hook_result_t result;
    uint8_t reg=0u,value=0u,read=0u;
    (void)ctx;
    if(s_pm_mode==SC_PM_SUSPEND && s_pm_waiting) {
        /* The earlier suspended script may still own the I2C buffer. */
        if(s_pm_flag==I2C_ASYNC_PENDING)return PM_HOOK_PENDING;
        s_pm_waiting=0u;
    }
    if(s_pm_mode==SC_PM_IDLE && !s_suspended) {
        s_wake_armed=0u;s_paused=0u;
        return PM_HOOK_DONE;
    }
    if(s_pm_mode!=SC_PM_RESUME) {
        s_pm_mode=SC_PM_RESUME;s_pm_step=0u;
        s_wake_armed=0u;
    }
    for(;;) {
        if(s_pm_step==10u) {
            s_pm_mode=SC_PM_IDLE;
            s_paused=0u;s_suspended=0u;
            s_last_sample=Millis()-SC_SAMPLE_MS;
            return PM_HOOK_DONE;
        }
        switch(s_pm_step) {
        case 0u:reg=SC7A20_CTRG_CTRL3;value=0u;break;
        case 1u:reg=SC7A20_STRG_INT1_SOURCE;read=1u;break;
        case 2u:reg=SC7A20_STRG_INT2_SOURCE;read=1u;break;
        case 3u:reg=SC7A20_CTRG_INT1_CFG;value=0u;break;
        case 4u:reg=SC7A20_CTRG_INT2_CFG;value=0u;break;
        case 5u:reg=SC7A20_CTRG_CTRL2;value=0u;break;
        case 6u:reg=SC7A20_CTRG_CTRL5;value=0u;break;
        case 7u:reg=SC7A20_CTRG_CTRL4;value=SC_CTRL4_RUN;break;
        case 8u:reg=SC7A20_CTRG_TEMP_CFG;value=0xC0u;break;
        case 9u:reg=SC7A20_CTRG_CTRL1;value=SC_CTRL1_RUN;break;
        default:return PM_HOOK_ERROR;
        }
        result=sc_pm_xfer(read,reg,value);
        if(result==PM_HOOK_ERROR) {
            s_pm_mode=SC_PM_IDLE;s_pm_waiting=0u;
            s_suspended=0u;s_paused=0u;
            sc_offline();
            return PM_HOOK_ERROR;
        }
        if(result==PM_HOOK_PENDING)return result;
        ++s_pm_step;
    }
}

static const pm_device_node_t s_node={
    "sc7a20",sc_prepare,sc_suspend,sc_resume,0,80u,0,0
};

void SC7A20Algo_Init(void) {
    s_stage=0u;s_waiting=0u;s_paused=0u;s_suspended=0u;
    s_pm_waiting=0u;s_pm_mode=SC_PM_IDLE;
    s_pm_step=0u;s_wake_armed=0u;s_pm_routes=0u;
    s_orientation=SC7A20_ORIENT_UNKNOWN;
    s_candidate=SC7A20_ORIENT_UNKNOWN;s_confirm=0u;
    s_pickup_wake = 1u;
    s_motion_wake = 0u;
    s_tool_mode=0u;s_orient_div=0u;
    memset(&s_sample,0,sizeof(s_sample));
    s_retry_at=Millis();
    sc_calibration_load();
    SC7A20_AsyncSetReady(0u);
    PT_INIT(&s_transfer_pt);
    (void)pm_device_register(&s_node);
}

void SC7A20Algo_AddWakeSettingsItems(ui_page_t *page) {
    static const char *const names[2]={" Pickup Wake"," Motion Wake"};
    uint8_t *state[2]={&s_pickup_wake,&s_motion_wake};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<2u;i++) {
        s_wake_data[i].name=names[i];
        s_wake_data[i].ptr=state[i];
        s_wake_data[i].dataType=UI_DATA_SWITCH;
        s_wake_data[i].actionType=UI_DATA_ACTION_RW;
        s_wake_elements[i].data=&s_wake_data[i];
        Create_element(&s_wake_items[i],&s_wake_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_wake_items[i],page,0,0);
    }
}

void SC7A20Algo_AddStatisticsItems(ui_page_t *page) {
    static const char *const names[3] = {" Accel X mg", " Accel Y mg", " Accel Z mg"};
    uint8_t i;
    if(!page)return;
    for(i=0u;i<3u;i++) {
        s_data[i].name=names[i];s_data[i].ptr=&s_axes[i];
        s_data[i].dataType=UI_DATA_INT;s_data[i].actionType=UI_DATA_ACTION_RO;
        s_elements[i].data=&s_data[i];
        Create_element(&s_items[i],&s_elements[i]);
        AddItem(names[i],UI_ITEM_DATA,0,&s_items[i],page,0,0);
    }
}
