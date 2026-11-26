/* The page owns only start/stop requests and the display, never PWM. */
#include "sos_app.h"
#include "wled_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"

/* Original PB10A SOS tool icon, 30 x 30 XBM. */
// clang-format off
const unsigned char img_sos[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x07, 0x00, 0x00, 0x0E, 0x1C, 0x00,
    0x80, 0xE3, 0x71, 0x00, 0xC0, 0xFC, 0xCF, 0x00, 0x60, 0x07, 0xB8, 0x01,
    0xB0, 0x01, 0x60, 0x03, 0xD8, 0x00, 0xC0, 0x06, 0x68, 0x00, 0x80, 0x05,
    0x2C, 0x00, 0x00, 0x0D, 0x34, 0x00, 0x00, 0x0B, 0x16, 0x00, 0x00, 0x1A,
    0x12, 0xC7, 0x71, 0x12, 0x9A, 0x28, 0x8A, 0x16, 0x9A, 0x20, 0x0A, 0x16,
    0x1A, 0x2F, 0x72, 0x16, 0x1A, 0x28, 0x82, 0x16, 0x92, 0x28, 0x8A, 0x12,
    0x16, 0xC7, 0x71, 0x1A, 0x34, 0x00, 0x00, 0x0B, 0x2C, 0x00, 0x00, 0x0D,
    0x68, 0x00, 0x80, 0x05, 0xD8, 0x00, 0xC0, 0x06, 0xB0, 0x01, 0x60, 0x03,
    0x60, 0x07, 0xB8, 0x01, 0xC0, 0xFC, 0xCF, 0x00, 0x80, 0xE3, 0x71, 0x00,
    0x00, 0x0E, 0x1C, 0x00, 0x00, 0xF8, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// clang-format on


static uint8_t s_warning_shown, s_started;

static void center_text(uint16_t y, const char *text) {
    Disp_DrawCentered(y, text, 0u);
}

void SOS_AppOnLeave(ui_t *ui) {
    (void)ui;
    s_warning_shown = s_started = 0u;
    WLED_AlgoRequestSOS(0u);
}

void SOS_AppRun(ui_t *ui) {
    if (!ui)
        return;
    if (ui->action != UI_ACTION_NONE) {
        if (!s_started && s_warning_shown && ui->action == UI_ACTION_ENTER) {
            s_started = 1u;
            ui->action = UI_ACTION_NONE;
            WLED_AlgoRequestSOS(1u);
        } else {
            WLED_AlgoRequestSOS(0u);
            return; /* MiaoUI exits WORD pages on any unconsumed action. */
        }
    }

    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    if (!s_started) {
        center_text(11u, "WARNING");
        Disp_DrawLine(0u, 14u, UI_HOR_RES - 1u, 14u);
        center_text(27u, "High intensity light");
        center_text(40u, "Do not look at LED");
        Disp_SetFont(font_home_h6w4);
        center_text(52u, "CONF: Start");
        center_text(62u, "Other keys: Exit");
        Disp_SetFont(UI_FONT);
        Disp_SendBuffer();
        s_warning_shown = 1u;
        UI_RequestNextFrame(ui, 200u);
        return;
    }

    uint8_t progress;
    wled_sos_state_t state = WLED_AlgoGetSOSStatus(&progress);
    center_text(11u, "SOS Blink");
    Disp_DrawLine(0u, 14u, UI_HOR_RES - 1u, 14u);
    static const char *const states[] = {
        "STOPPED", "WAIT DATA", "TRANSMITTING", "COOLING", "PROTECTED"};
    center_text(30u, states[state]);
    Disp_SetFont(font_home_h6w4);
    center_text(46u, "Any key: Exit");
    Disp_DrawFrame(7u, 55u, 114u, 7u);
    if (progress)
        Disp_DrawBox(9u, 57u, 110u * progress / 100u, 3u);
    Disp_SetFont(UI_FONT);
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, 50u);
}
