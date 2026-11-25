/* The page owns only start/stop requests and the display, never PWM. */
#include "sos_app.h"
#include "wled_algo.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"

static void center_text(uint16_t y, const char *text) {
    uint16_t width = Disp_GetUTF8Width(text);
    Disp_DrawStr(width < UI_HOR_RES ? (UI_HOR_RES - width) / 2u : 0u, y, text);
}

void SOS_AppOnLeave(ui_t *ui) {
    (void)ui;
    /* Closing an unused tool must not switch off manual illumination. */
    WLED_AlgoRequestSOS(0u);
}

void SOS_AppRun(ui_t *ui) {
    if (!ui)
        return;
    if (ui->action == UI_ACTION_BACK) {
        WLED_AlgoRequestSOS(0u);
        return;
    }

    uint8_t progress;
    wled_sos_state_t state = WLED_AlgoGetSOSStatus(&progress);
    uint8_t running = state == WLED_SOS_WAIT_METER ||
                      state == WLED_SOS_TRANSMIT || state == WLED_SOS_COOLDOWN;
    if (ui->action == UI_ACTION_ENTER)
        WLED_AlgoRequestSOS(!running);
    ui->action = UI_ACTION_NONE;

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
    center_text(42u, running ? "CONF: Stop" : "CONF: Start");
    center_text(50u, "Long CONF: Back");
    Disp_DrawFrame(7u, 55u, 114u, 7u);
    if (progress)
        Disp_DrawBox(9u, 57u, 110u * progress / 100u, 3u);
    Disp_SetFont(UI_FONT);
    Disp_SendBuffer();
    UI_RequestNextFrame(ui, 50u);
}
