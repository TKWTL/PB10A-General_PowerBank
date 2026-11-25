/* The page owns only start/stop requests and the display, never PWM. */
#include "sos_app.h"
#include "wled_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"

/* Original PB10A SOS tool icon, 30 x 30 XBM. */
const unsigned char img_sos[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x07, 0x00, 0x00, 0xFE, 0x1F, 0x00,
    0x80, 0x03, 0x70, 0x00, 0xC0, 0x00, 0xC0, 0x00, 0x60, 0x00, 0x80, 0x01,
    0x30, 0x00, 0x00, 0x03, 0x18, 0x00, 0x00, 0x06, 0x08, 0x00, 0x00, 0x04,
    0x0C, 0x00, 0x00, 0x0C, 0xE4, 0xE1, 0xE1, 0x09, 0x16, 0x12, 0x12, 0x1A,
    0x16, 0x10, 0x12, 0x18, 0x16, 0x10, 0x12, 0x18, 0xE6, 0x11, 0xE2, 0x19,
    0x06, 0x12, 0x02, 0x1A, 0x06, 0x12, 0x02, 0x1A, 0x16, 0x12, 0x12, 0x1A,
    0xE6, 0xE1, 0xE1, 0x19, 0x04, 0x00, 0x00, 0x08, 0x0C, 0x00, 0x00, 0x0C,
    0x08, 0x00, 0x00, 0x04, 0x18, 0x00, 0x00, 0x06, 0x30, 0x00, 0x00, 0x03,
    0x60, 0x00, 0x80, 0x01, 0xC0, 0x00, 0xC0, 0x00, 0x80, 0x03, 0x70, 0x00,
    0x00, 0xFE, 0x1F, 0x00, 0x00, 0xF8, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
};

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
