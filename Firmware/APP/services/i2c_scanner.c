/* Collar-Radio 128x64 I2C scanner rewritten for nonblocking coroOS.
 * One address probe per UI tick; ENTER rescans, UP/DOWN pages results,
 * BACK exits. Never use osDelay/vTaskDelay in a MiaoUI callback.
 */
#include "i2c_scanner.h"
#include "bsp_i2c.h"
#include "display/dispDriver.h"
#include "pm_api.h"
#include <stdio.h>
#define SCAN_MIN_ADDR 0x08u
#define SCAN_MAX_ADDR 0x77u
#define SCAN_IDLE_MS 5000u
#define PM_BLOCK_I2C_SCAN 0x02u
static uint8_t s_scanning,s_next,s_count,s_page,s_active;
static uint8_t s_found[SCAN_MAX_ADDR-SCAN_MIN_ADDR+1u];
static uint32_t s_next_scan_ms;
static void scanner_reset(void)
{
    s_next=SCAN_MIN_ADDR;s_count=0u;s_page=0u;s_scanning=1u;
}
void i2c_Scanner(ui_t *ui)
{
    char line[24];
    uint8_t fg,row,i;
    uint32_t now=HAL_GetTick();
    if(!ui)return;
    if(ui->action==UI_ACTION_BACK)
    {
        pm_api_set_sleep_block(PM_BLOCK_I2C_SCAN,0u);
        s_active=0u;
        return;
    }
    if(!s_active)
    {
        s_active=1u;
        scanner_reset();
        pm_api_set_sleep_block(PM_BLOCK_I2C_SCAN,1u);
    }
    if(ui->action==UI_ACTION_ENTER)
        scanner_reset();
    if(ui->action==UI_ACTION_UP && s_page>0u)--s_page;
    if(ui->action==UI_ACTION_DOWN && (uint16_t)(s_page+1u)*8u<s_count)++s_page;
    /* The WORD page callback must consume keys other than BACK. */
    ui->action=UI_ACTION_NONE;
    if(s_scanning)
    {
        if(I2C_IsReady((uint16_t)s_next<<1))
            s_found[s_count++]=s_next;
        if(s_next++>=SCAN_MAX_ADDR)
        {
            s_scanning=0u;s_next_scan_ms=now+SCAN_IDLE_MS;
        }
    }
    else if((int32_t)(now-s_next_scan_ms)>=0)
        scanner_reset();
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    snprintf(line,sizeof(line),"I2C %s",s_scanning?"SCAN":"DONE");
    Disp_DrawStr(0u,12u,line);
    snprintf(line,sizeof(line),"%u found",s_count);
    Disp_DrawStr(75u,12u,line);
    for(row=0u;row<4u;row++)
    {
        i=(uint8_t)(s_page*8u+row);
        if(i>=s_count)break;
        snprintf(line,sizeof(line),"0x%02X",s_found[i]);
        Disp_DrawStr(3u,(uint16_t)(26u+row*12u),line);
        if((uint16_t)i+4u<s_count)
        {
            snprintf(line,sizeof(line),"0x%02X",s_found[i+4u]);
            Disp_DrawStr(66u,(uint16_t)(26u+row*12u),line);
        }
    }
    Disp_SendBuffer();
}
