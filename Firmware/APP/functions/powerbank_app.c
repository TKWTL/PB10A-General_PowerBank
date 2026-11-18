/* MiaoUI-installed Powerbank application entry point. Hardware control
 * will be added after verifying the SW6306 control/power sequence. */
#include "powerbank_app.h"
#include "display/dispDriver.h"
void Powerbank_AppRun(ui_t *ui)
{
    uint8_t fg;
    if(!ui || ui->action!=UI_ACTION_NONE)return;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0,0,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(0,13,"Powerbank");
    Disp_DrawStr(0,31,"SW6306 services");
    Disp_DrawStr(0,49,"Not started");
    Disp_SendBuffer();
}
