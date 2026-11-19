/* PB10A interim SW6306 telemetry page.
 * The UI never performs I2C: it displays the coherent foreground cache
 * updated by the independent 100ms SW6306 service coroutine. */
#include "powerbank_app.h"
#include "power_algo.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include <stdio.h>
void Powerbank_AppRun(ui_t *ui)
{
    power_monitor_t m;
    uint8_t fg;
    char line[32];
    if(!ui)return;
    if(ui->action==UI_ACTION_BACK)return; /* WORD page exits on long CONF */
    ui->action=UI_ACTION_NONE; /* do not exit on other key actions */
    PowerAlgo_GetMonitor(&m);
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(font_radio_h12w6);
    if(!m.initialized)
    {
        Disp_DrawStr(3u,12u,"SW6306 INIT...");
        Disp_DrawStr(3u,30u,"BUS/BAT --");
        Disp_DrawStr(3u,48u,"No live data");
    }
    else if(!m.valid)
    {
        Disp_DrawStr(3u,12u,"SW6306 WAIT...");
        Disp_DrawStr(3u,30u,"Data not fresh");
    }
    else
    {
        (void)snprintf(line,sizeof(line),"C1:%u A1:%u %s",
                       (unsigned)m.c1_on,(unsigned)m.a1_on,
                       m.charging?"CHG":m.discharging?"DIS":"IDLE");
        Disp_DrawStr(0u,11u,line);
        (void)snprintf(line,sizeof(line),"BUS %5umV %4umA",
                       (unsigned)m.vbus_mv,(unsigned)m.ibus_ma);
        Disp_DrawStr(0u,23u,line);
        (void)snprintf(line,sizeof(line),"BAT %5umV %4umA",
                       (unsigned)m.vbat_mv,(unsigned)m.ibat_ma);
        Disp_DrawStr(0u,35u,line);
        if(m.capacity_valid)
            (void)snprintf(line,sizeof(line),"CAP %3u%%", (unsigned)m.capacity_pct);
        else (void)snprintf(line,sizeof(line),"CAP --%%");
        Disp_DrawStr(0u,47u,line);
        (void)snprintf(line,sizeof(line),"PROTO: %.15s",
                       m.protocol?m.protocol:"--");
        Disp_DrawStr(0u,59u,line);
    }
    Disp_SendBuffer();
}
