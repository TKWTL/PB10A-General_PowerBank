/* The page owns only start/stop requests and the display, never PWM. */
#include "sos_app.h"
#include "wled_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"

/* Original PB10A SOS tool icon, 30 x 30 XBM. */
// clang-format off
const unsigned char img_sos[] = {
    0x00, 0xFC, 0x0F, 0x00, 0x00, 0x07, 0x38, 0x00, 0xC0, 0xF1, 0xE3, 0x00,
    0x60, 0xFC, 0x8F, 0x01, 0x30, 0xFF, 0x3F, 0x03, 0x98, 0xFF, 0x7F, 0x06,
    0xCC, 0xFF, 0xFF, 0x0C, 0xE4, 0xFF, 0xFF, 0x09, 0xF6, 0xFF, 0xFF, 0x1B,
    0xF2, 0xFF, 0xFF, 0x13, 0x1B, 0x1E, 0x1E, 0x36, 0xE9, 0xED, 0xED, 0x25,
    0xED, 0xEF, 0xED, 0x2F, 0xED, 0xEF, 0xED, 0x2F, 0x1D, 0xEE, 0x1D, 0x2E,
    0xFD, 0xED, 0xFD, 0x2D, 0xFD, 0xED, 0xFD, 0x2D, 0xED, 0xED, 0xED, 0x2D,
    0x19, 0x1E, 0x1E, 0x26, 0xFB, 0xFF, 0xFF, 0x37, 0xF2, 0xFF, 0xFF, 0x13,
    0xF6, 0xFF, 0xFF, 0x1B, 0xE4, 0xFF, 0xFF, 0x09, 0xCC, 0xFF, 0xFF, 0x0C,
    0x98, 0xFF, 0x7F, 0x06, 0x30, 0xFF, 0x3F, 0x03, 0x60, 0xFC, 0x8F, 0x01,
    0xC0, 0xF1, 0xE3, 0x00, 0x00, 0x07, 0x38, 0x00, 0x00, 0xFC, 0x0F, 0x00,
};
// clang-format on


static uint8_t s_entered;

static void center_text(uint16_t y, const char *text) {
    uint16_t width = Disp_GetUTF8Width(text);
    Disp_DrawStr(width < UI_HOR_RES ? (UI_HOR_RES - width) / 2u : 0u, y, text);
}

void SOS_AppOnLeave(ui_t *ui) {
    (void)ui;
    s_entered = 0u;
    WLED_AlgoRequestSOS(0u);
}

void SOS_AppRun(ui_t *ui) {
    if (!ui)
        return;
    if (ui->action != UI_ACTION_NONE) {
        WLED_AlgoRequestSOS(0u);
        return; /* MiaoUI exits WORD pages on any unconsumed action. */
    }
    if (!s_entered) {
        s_entered = 1u;
        WLED_AlgoRequestSOS(1u);
    }

    uint8_t progress;
    wled_sos_state_t state = WLED_AlgoGetSOSStatus(&progress);

    uint8_t fg = ui->bgColor ^ 1u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u, 0u, UI_HOR_RES, UI_VER_RES);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    center_text(11u, "SOS Blink");
    Disp_DrawLine(0u, 14u, UI_HOR_RES - 1u, 14u);
    static const char *const states[] = {
        "STOPPED", "WAIT METER", "TRANSMITTING", "COOLING", "PROTECTED"};
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
