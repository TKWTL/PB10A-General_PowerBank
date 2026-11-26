/* Standard, pressure-calibrated and relative altitude. */
#include "altimeter_app.h"
#include "mini_crc.h"
#include "tools_motion_app.h"
#include "bmp180_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "pm_api.h"
#include "mini_math.h"
#include "mini_format.h"
#include "indev/indevDriver.h"
#include "i18n.h"
#include "main.h"

/* Original PB10A altimeter icon, 30 x 30 XBM. */
// clang-format off
const unsigned char img_altimeter[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x1C, 0x00, 0x00,
    0x00, 0x1E, 0x00, 0x00, 0x00, 0x3E, 0x00, 0x00, 0x00, 0x37, 0x00, 0x00,
    0x00, 0x73, 0x10, 0x00, 0x80, 0x63, 0x38, 0x00, 0x80, 0xE1, 0x3C, 0x00,
    0xC0, 0xC1, 0x7E, 0x00, 0xC0, 0xE7, 0x7F, 0x00, 0xE0, 0xFF, 0xE7, 0x00,
    0xE0, 0x99, 0xC3, 0x00, 0x70, 0x00, 0xC1, 0x01, 0x70, 0x00, 0x80, 0x01,
    0x38, 0x00, 0x80, 0x03, 0x38, 0x00, 0x00, 0x03, 0x18, 0x00, 0x00, 0x07,
    0x1C, 0x00, 0x00, 0x06, 0x0C, 0x00, 0x00, 0x0E, 0x0E, 0x00, 0x00, 0x0C,
    0xFE, 0xFF, 0xFF, 0x1F, 0xFC, 0xFF, 0xFF, 0x1F, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on

#define ALTIMETER_FRAME_MS 200u
#define ALT_CAL_SAMPLES 16u
#define ALT_REFERENCE_TAG 0x4132u
/* Round(30 kPa * 101325 / 110 kPa) through 110 kPa * 101325 / 30 kPa. */
#define ALT_REFERENCE_MIN_PA 27634u
#define ALT_REFERENCE_MAX_PA 371525u
#define ALT_LEFT 4u
#define ALT_RIGHT 124u
#define ALT_TOP 3u
#define ALT_BOTTOM 61u

typedef enum { ALT_CAL,
               ALT_REL,
               ALT_STD,
               ALT_MODE_COUNT } alt_mode_t;
typedef enum { ALT_MEASURE,
               ALT_EDIT,
               ALT_SAMPLING } alt_view_t;
typedef enum { ALT_STATUS_NONE,
               ALT_STATUS_SAVED,
               ALT_STATUS_PRESSURE } alt_status_t;

typedef struct {
    uint32_t reference_pa;
    uint32_t crc;
    uint32_t tag;
} alt_cal_record_t;

static volatile alt_cal_record_t s_cal_record __attribute__((section(".bss.noinit"), used, aligned(4)));
static uint8_t s_active, s_zero_valid, s_repeat_count;
static uint8_t s_sample_count;
static alt_mode_t s_mode;
static alt_view_t s_view;
static alt_status_t s_status;
static int32_t s_zero_dm, s_edit_m;
static uint32_t s_reference_pa, s_last_seq, s_pressure_sum;

static uint32_t reference_load(void) {
    uint32_t pressure = s_cal_record.reference_pa;
    if (s_cal_record.tag != ALT_REFERENCE_TAG ||
        pressure < ALT_REFERENCE_MIN_PA || pressure > ALT_REFERENCE_MAX_PA ||
        s_cal_record.crc != crc32_words3(pressure, 0u, 0u))
        return 0u;
    return pressure;
}

static void reference_save(uint32_t pressure) {
    s_cal_record.tag = 0u;
    __DMB();
    s_cal_record.reference_pa = pressure;
    s_cal_record.crc = crc32_words3(pressure, 0u, 0u);
    __DMB();
    s_cal_record.tag = ALT_REFERENCE_TAG;
    __DSB();
    s_reference_pa = pressure;
}

/* 101325 = 525 * 193. Split the first product into quotient and remainder
 * before multiplying by 193. For the supported pressure and reference
 * ranges both products stay below 72 million; round once at the end. */
static uint32_t standard_ratio_pa(uint32_t pressure, uint32_t reference) {
    uint32_t partial = pressure * 525u;
    return (partial / reference) * 193u +
           ((partial % reference) * 193u + reference / 2u) / reference;
}

static uint8_t pressure_in_range(int32_t pressure) {
    return pressure >= PRESSURE_ALT_MIN_PA && pressure <= PRESSURE_ALT_MAX_PA;
}

static uint8_t calibrated_height(int32_t pressure, int32_t *height_dm) {
    return s_reference_pa && pressure_in_range(pressure) &&
           pressure_altitude_dm((int32_t)standard_ratio_pa((uint32_t)pressure, s_reference_pa), height_dm);
}

static void save_calibration(void) {
    int32_t standard_pa;
    if (!altitude_pressure_pa(s_edit_m * 10, &standard_pa)) {
        s_view = ALT_EDIT;
        return;
    }

    uint32_t measured = (s_pressure_sum + ALT_CAL_SAMPLES / 2u) / ALT_CAL_SAMPLES;
    uint32_t reference = standard_ratio_pa(measured, (uint32_t)standard_pa);
    uint32_t normalized = standard_ratio_pa(measured, reference);
    /* A one-Pa reference rounding at an endpoint can place the normalized
     * pressure just outside the table. Use the adjacent reference then. */
    if (normalized < PRESSURE_ALT_MIN_PA)
        --reference;
    else if (normalized > PRESSURE_ALT_MAX_PA)
        ++reference;
    reference_save(reference);
    s_view = ALT_MEASURE;
    s_status = ALT_STATUS_SAVED;
}

static void draw_right(uint16_t y, const char *text) {
    uint16_t width = Disp_GetUTF8Width(text);
    Disp_DrawStr(width <= ALT_RIGHT - ALT_LEFT ? ALT_RIGHT - width : ALT_LEFT, y, text);
}

static void draw_center(uint16_t y, const char *text) {
    Disp_DrawCentered(y, text, ALT_LEFT);
}

static void draw_height(int32_t value, uint8_t decimals) {
    char number[12];
    uint8_t negative = value < 0;
    UI_FmtScaled(number, sizeof(number), negative ? -value : value, decimals);

    Disp_SetFont(font_radio_h24w12);
    uint16_t width = Disp_GetUTF8Width(number);
    /* The large numeric font has no minus glyph. Reserve space for a line
     * on the left and the unit in the normal font on the right. */
    uint16_t x = (UI_HOR_RES - width - 9u - (negative ? 9u : 0u)) / 2u;
    if (negative) {
        Disp_DrawBox(x, 26u, 7u, 2u);
        x += 9u;
    }
    Disp_DrawStr(x, 37u, number);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(x + width + 3u, 37u, "m");
}

static void draw_measurement(const bmp180_sample_t *sample, uint8_t sample_valid,
                             int32_t standard_dm, uint8_t standard_valid) {
    static const char *const modes[] = {"Calib. Height", "Relative Height", "Standard Height"};
    char text[24], value[16];
    Disp_DrawStr(ALT_LEFT, 13u, I18N_Text(modes[s_mode]));
    mini_snprintf(text, sizeof(text), "%u/3", (unsigned)s_mode + 1u);
    draw_right(13u, text);

    int32_t height_dm = standard_dm;
    uint8_t valid = standard_valid;
    if (s_mode == ALT_CAL) {
        valid = sample_valid && calibrated_height(sample->pressure_pa, &height_dm);
    } else if (s_mode == ALT_REL) {
        valid = standard_valid && s_zero_valid;
        height_dm -= s_zero_dm;
    }

    if (valid) {
        draw_height(height_dm, 1u);
    } else if (s_mode == ALT_CAL && !s_reference_pa) {
        draw_center(25u, "-- m");
        draw_center(37u, I18N_Text("No calibration"));
    } else {
        draw_center(33u, "-- m");
    }

    Disp_DrawLine(ALT_LEFT, 39u, ALT_RIGHT - 1u, 39u);
    if (sample_valid) {
        UI_FmtScaled(value, sizeof(value), sample->pressure_pa / 10, 1u);
        mini_snprintf(text, sizeof(text), "%shPa", value);
        Disp_DrawStr(ALT_LEFT, 49u, text);
        UI_FmtScaled(value, sizeof(value), sample->temperature_c10, 1u);
        mini_snprintf(text, sizeof(text), "%s°C", value);
        draw_right(49u, text);
    } else {
        Disp_DrawStr(ALT_LEFT, 49u, "--hPa");
        draw_right(49u, "--°C");
    }
    Disp_DrawStr(ALT_LEFT, 60u, I18N_Text("L/R:Mode"));
    const char *hint = "CONF:Exit";
    if (s_mode == ALT_CAL)
        hint = s_status == ALT_STATUS_SAVED ? "Cal saved" : "U/D:Cal";
    else if (s_mode == ALT_REL)
        hint = "U/D:Zero";
    draw_right(60u, I18N_Text(hint));
}

static void draw_editor(void) {
    draw_center(13u, I18N_Text("Set Altitude"));
    draw_height(s_edit_m, 0u);
    draw_center(49u, I18N_Text(s_status == ALT_STATUS_PRESSURE ? "Pressure unavailable" : "U/D:Adjust"));
    Disp_DrawStr(ALT_LEFT, 60u, I18N_Text("L:Cancel"));
    draw_right(60u, I18N_Text("R:Save"));
}

static void draw_sampling(void) {
    char text[12];
    draw_center(13u, I18N_Text("Calibrating"));
    draw_center(30u, I18N_Text("Keep still"));
    Disp_DrawFrame(8u, 38u, 112u, 8u);
    if (s_sample_count)
        Disp_DrawBox(10u, 40u, 108u * s_sample_count / ALT_CAL_SAMPLES, 4u);
    mini_snprintf(text, sizeof(text), "%u/%u", (unsigned)s_sample_count, ALT_CAL_SAMPLES);
    draw_center(60u, text);
    Disp_DrawStr(ALT_LEFT, 60u, I18N_Text("L:Cancel"));
}

static void handle_action(UI_ACTION action, const bmp180_sample_t *sample, uint8_t sample_valid,
                          int32_t standard_dm, uint8_t standard_valid) {
    /* On the Tools icon page LEFT/RIGHT send UP/DOWN, and screen UP/DOWN
     * send MINUS/PLUS after screen-relative key mapping. */
    uint8_t vertical = action == UI_ACTION_MINUS || action == UI_ACTION_PLUS;
    if (s_view == ALT_MEASURE) {
        if (action == UI_ACTION_NONE || indevActionIsRepeat())
            return;
        s_status = ALT_STATUS_NONE;
        if (action == UI_ACTION_UP)
            s_mode = (s_mode + ALT_MODE_COUNT - 1u) % ALT_MODE_COUNT;
        else if (action == UI_ACTION_DOWN)
            s_mode = (s_mode + 1u) % ALT_MODE_COUNT;
        else if (vertical && s_mode == ALT_CAL) {
            int32_t initial_dm = standard_dm;
            if (sample_valid)
                (void)calibrated_height(sample->pressure_pa, &initial_dm);
            s_edit_m = (initial_dm + (initial_dm < 0 ? -5 : 5)) / 10;
            s_repeat_count = 0u;
            s_view = ALT_EDIT;
        } else if (vertical && s_mode == ALT_REL && standard_valid) {
            s_zero_dm = standard_dm;
            s_zero_valid = 1u;
        }
    } else if (action == UI_ACTION_UP && !indevActionIsRepeat()) {
        s_view = ALT_MEASURE;
        s_status = ALT_STATUS_NONE;
    } else if (s_view == ALT_EDIT && vertical) {
        if (!indevActionIsRepeat())
            s_repeat_count = 0u;
        else if (s_repeat_count < 15u)
            ++s_repeat_count;
        int32_t step = s_repeat_count >= 15u ? 100 : s_repeat_count >= 5u ? 10
                                                                          : 1;
        s_edit_m += action == UI_ACTION_MINUS ? step : -step;
        if (s_edit_m < PRESSURE_ALT_MIN_M)
            s_edit_m = PRESSURE_ALT_MIN_M;
        if (s_edit_m > PRESSURE_ALT_MAX_M)
            s_edit_m = PRESSURE_ALT_MAX_M;
    } else if (s_view == ALT_EDIT && action == UI_ACTION_DOWN && !indevActionIsRepeat()) {
        if (!standard_valid) {
            s_status = ALT_STATUS_PRESSURE;
            return;
        }
        s_pressure_sum = 0u;
        s_sample_count = 0u;
        s_last_seq = sample->seq;
        s_view = ALT_SAMPLING;
    }
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
    if (!ui || ui->action == UI_ACTION_ENTER || ui->action == UI_ACTION_BACK)
        return;
    if (!s_active) {
        s_active = 1u;
        s_zero_valid = 0u;
        s_mode = ALT_CAL;
        s_view = ALT_MEASURE;
        s_status = ALT_STATUS_NONE;
        s_reference_pa = reference_load();
        pm_api_set_sleep_block(SENSOR_TOOL_PM_BLOCK, 1u);
        pm_api_refresh_idle();
    }

    bmp180_sample_t sample;
    int32_t standard_dm = 0;
    uint8_t sample_valid = BMP180Algo_GetSample(&sample);
    uint8_t standard_valid = sample_valid && pressure_altitude_dm(sample.pressure_pa, &standard_dm);
    if (standard_valid && !s_zero_valid) {
        s_zero_dm = standard_dm;
        s_zero_valid = 1u;
    }

    handle_action(ui->action, &sample, sample_valid, standard_dm, standard_valid);
    ui->action = UI_ACTION_NONE;
    if (s_view == ALT_SAMPLING) {
        if (!standard_valid) {
            s_view = ALT_EDIT;
            s_status = ALT_STATUS_PRESSURE;
        } else if (sample.seq != s_last_seq) {
            s_last_seq = sample.seq;
            s_pressure_sum += (uint32_t)sample.pressure_pa;
            if (++s_sample_count == ALT_CAL_SAMPLES)
                save_calibration();
        }
    }

    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetClipWindow(ALT_LEFT, ALT_TOP, ALT_RIGHT, ALT_BOTTOM);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    if (s_view == ALT_MEASURE)
        draw_measurement(&sample, sample_valid, standard_dm, standard_valid);
    else if (s_view == ALT_EDIT)
        draw_editor();
    else
        draw_sampling();
    Disp_SetMaxClipWindow();
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, ALTIMETER_FRAME_MS);
}
