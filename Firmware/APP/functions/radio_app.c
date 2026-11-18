/* MiaoUI Radio app entry point; no unsolicited RDA5807 power-on. */
#include "radio_app.h"
#include "display/dispDriver.h"
void Radio_AppRun(ui_t *ui)
{
    uint8_t fg;
    if(!ui || ui->action!=UI_ACTION_NONE)return;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0,0,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(0,13,"FM Radio");
    Disp_DrawStr(0,31,"RDA5807 driver");
    Disp_DrawStr(0,49,"Not started");
    Disp_SendBuffer();
}
