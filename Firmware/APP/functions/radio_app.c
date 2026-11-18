/* PB10A FM bring-up (fixed search mode). Derived from the operational
 * flow in Collar-Radio Application/Applications/radio.c, retaining its
 * RDA5807 driver register model. No nested while(1) and no FreeRTOS APIs.
 *
 * Enter: wake radio, initialize and start ONE seek automatically.
 * UP/DOWN: seek lower/higher, ENTER: seek higher again, BACK: power off.
 * Bus transfers remain IRQ-driven by BSP/I2C and foreground-completion
 * waited; no long software delay is used in this MiaoUI callback.
 */
#include "radio_app.h"
#include "rda5807m.h"
#include "bsp_i2c.h"
#include "pm_api.h"
#include "millis.h"
#include "main.h"
#include "fonts/fonts.h"
#include "display/dispDriver.h"
#include <stdio.h>

enum { RADIO_OFF=0, RADIO_RESET_WAIT, RADIO_POWER_WAIT, RADIO_TUNE_WAIT,
       RADIO_SEARCH, RADIO_READY, RADIO_ERROR };
#define RADIO_PM_BLOCK 0x04u
#define RADIO_QUERY_MS 180u
#define RADIO_SEEK_TIMEOUT_MS 8000u
#define RADIO_MIN_VOLUME 0u /* minimum gain is NOT mute on tested board */
static uint8_t s_state, s_first_seek_pending, s_search_failed;
static uint8_t s_volume, s_stereo, s_rssi, s_audio_muted, s_unmute_armed;
static uint16_t s_freq_mhz10; /* e.g. 1017 = 101.7 MHz */
static uint32_t s_state_at, s_last_query, s_unmute_arm_at;
static uint8_t s_error_code;

static uint8_t radio_write16(uint8_t reg,uint16_t value)
{
    uint8_t data[2]={(uint8_t)(value>>8),(uint8_t)value};
    return I2C_RegWrite(RDA5807_Wr_REG_Addr,reg,data,2u);
}
static uint8_t radio_read16(uint8_t reg,uint16_t *value)
{
    uint8_t data[2];
    if(!value || I2C_RegRead(RDA5807_Wr_REG_Addr,reg,data,2u)!=I2C_OK)
        return I2C_ERROR;
    *value=(uint16_t)(((uint16_t)data[0]<<8)|data[1]);
    return I2C_OK;
}
static void radio_fail(uint8_t why)
{
    s_state=RADIO_ERROR;
    s_error_code=why;
}
static uint8_t radio_read_status(uint16_t *status,uint16_t *signal)
{
    uint8_t ok=I2C_ERROR;
    if(I2C_TransactionBegin()!=I2C_OK)return I2C_ERROR;
    if(radio_read16(RDA5807_REG0A,status)==I2C_OK &&
       radio_read16(RDA5807_REG0B,signal)==I2C_OK)ok=I2C_OK;
    I2C_TransactionEnd();
    return ok;
}
/* Collar-Radio SEEK_UP/SEEK_DOWN sequence; unlike StartSeek(), this does
 * not rely on a previous STC bit being set to accept the request. */
static void radio_seek(uint8_t upward)
{
    uint8_t ok=I2C_ERROR;
    if(I2C_TransactionBegin()!=I2C_OK){radio_fail(4u);return;}
    if(upward)Register[0]|=RDA5807_REG02_SEEKUP;
    else Register[0]&=(uint16_t)~RDA5807_REG02_SEEKUP;
    Register[0]|=RDA5807_REG02_SEEK;
    Register[1]&=(uint16_t)~RDA5807_REG03_TUNE;
    if(radio_write16(RDA5807_REG02,Register[0])==I2C_OK &&
       radio_write16(RDA5807_REG03,Register[1])==I2C_OK)ok=I2C_OK;
    I2C_TransactionEnd();
    if(ok!=I2C_OK){radio_fail(4u);return;}
    s_state=RADIO_SEARCH;
    s_state_at=Millis();
    s_last_query=s_state_at;
    s_search_failed=0u;
    s_first_seek_pending=0u;
}
void Radio_AppStart(void)
{
    if(s_state!=RADIO_OFF)return;
    s_error_code=0u;s_search_failed=0u;s_first_seek_pending=1u;
    s_volume=RADIO_MIN_VOLUME;s_stereo=0u;s_rssi=0u;s_freq_mhz10=870u;
    s_audio_muted=1u;s_unmute_armed=0u; /* VOL=0 alone is still loud. */
    Register[0]&=(uint16_t)~(RDA5807_REG02_DMUTE|RDA5807_REG02_BASS);
    pm_api_set_sleep_block(RADIO_PM_BLOCK,1u); /* MCO clock must not stop */
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_LSE,LL_RCC_MCO1_DIV_1);
    if(radio_write16(RDA5807_REG02,RDA5807_REG02_SRST)!=I2C_OK)
    {radio_fail(1u);return;}
    s_state=RADIO_RESET_WAIT;
    s_state_at=Millis();
}
void Radio_AppStop(void)
{
    if(s_state!=RADIO_OFF && s_state!=RADIO_ERROR)
    {
        Register[0]&=(uint16_t)~(RDA5807_REG02_ENABLE|RDA5807_REG02_DMUTE);
        (void)radio_write16(RDA5807_REG02,Register[0]);
    }
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_NOCLOCK,LL_RCC_MCO1_DIV_1);
    pm_api_set_sleep_block(RADIO_PM_BLOCK,0u);
    s_audio_muted=1u;s_unmute_armed=0u;s_first_seek_pending=1u;
    s_state=RADIO_OFF;
}
void Radio_AppPoll(void)
{
    uint32_t now=Millis();
    uint16_t status=0u, signal=0u;
    if(s_state==RADIO_RESET_WAIT)
    {
        if((uint32_t)(now-s_state_at)<50u)return;
        Register[0]|=(RDA5807_REG02_ENABLE|
                      RDA5807_REG02_DHIZ|RDA5807_REG02_SEEKUP);
        Register[0]&=(uint16_t)~(RDA5807_REG02_DMUTE|RDA5807_REG02_BASS);
        Register[0]&=(uint16_t)~RDA5807_REG02_SEEK;
        if(radio_write16(RDA5807_REG02,Register[0])!=I2C_OK)
        {radio_fail(2u);return;}
        s_state=RADIO_POWER_WAIT;s_state_at=now;
        return;
    }
    if(s_state==RADIO_POWER_WAIT)
    {
        uint8_t ok=I2C_ERROR;
        if((uint32_t)(now-s_state_at)<400u)return;
        if(I2C_TransactionBegin()!=I2C_OK){radio_fail(3u);return;}
        Register[1]=RDA5807_FreqToReg(87u*40u,TUNE);
        Register[3]=(uint16_t)((Register[3]&~RDA5807_REG05_VOL_MSK)|s_volume);
        if(radio_write16(RDA5807_REG03,Register[1])==I2C_OK &&
           radio_write16(RDA5807_REG04,Register[2])==I2C_OK &&
           radio_write16(RDA5807_REG05,Register[3])==I2C_OK)ok=I2C_OK;
        I2C_TransactionEnd();
        if(ok!=I2C_OK){radio_fail(3u);return;}
        s_state=RADIO_TUNE_WAIT;s_state_at=now;s_last_query=now-200u;
        return;
    }
    if(s_state!=RADIO_TUNE_WAIT && s_state!=RADIO_SEARCH && s_state!=RADIO_READY)
        return;
    if((uint32_t)(now-s_last_query)<RADIO_QUERY_MS)return;
    s_last_query=now;
    if(radio_read_status(&status,&signal)!=I2C_OK){radio_fail(5u);return;}
    s_rssi=(uint8_t)((signal&RDA5807_REG0B_RSSI_MSK)>>9);
    s_stereo=(uint8_t)((status&RDA5807_REG0A_ST)!=0u);
    s_freq_mhz10=(uint16_t)(870u+(status&RDA5807_REG0A_CHAN_MSK));
    if(s_state==RADIO_TUNE_WAIT)
    {
        /* Initial auto-search is a one-shot per entry, after startup tune. */
        if((status&RDA5807_REG0A_STC)!=0u ||
           (uint32_t)(now-s_state_at)>=1200u)
        {
            if(s_first_seek_pending)radio_seek(1u);
            else s_state=RADIO_READY;
        }
    }
    else if(s_state==RADIO_SEARCH)
    {
        if((status&RDA5807_REG0A_STC)!=0u &&
           (uint32_t)(now-s_state_at)>=250u)
        {
            s_search_failed=(uint8_t)((status&RDA5807_REG0A_SF)!=0u);
            s_state=RADIO_READY;
        }
        else if((uint32_t)(now-s_state_at)>RADIO_SEEK_TIMEOUT_MS)
            radio_fail(6u);
    }
}
uint8_t Radio_AppAudioIsMuted(void){return s_audio_muted;}
/* ENTER deliberately toggles the real hardware DMUTE bit. At VOL=0 the
 * headphones were still loud: confirm listening level BEFORE unmuting. */
void Radio_AppToggleAudio(void)
{
    uint16_t reg;
    if(s_state!=RADIO_READY || I2C_TransactionBegin()!=I2C_OK)return;
    reg=Register[0];
    if(s_audio_muted)
    {
        /* Avoid a single accidental ENTER exposing loud headphone audio.
         * Explicit two-stage acknowledgement expires after 3 s. */
        if(!s_unmute_armed || (uint32_t)(Millis()-s_unmute_arm_at)>3000u)
        {
            s_unmute_armed=1u;s_unmute_arm_at=Millis();
            I2C_TransactionEnd();return;
        }
        reg|=RDA5807_REG02_DMUTE;
    }
    else reg&=(uint16_t)~RDA5807_REG02_DMUTE;
    if(radio_write16(RDA5807_REG02,reg)==I2C_OK)
    {Register[0]=reg;s_audio_muted=(uint8_t)!s_audio_muted;
     s_unmute_armed=0u;}
    I2C_TransactionEnd();
}
uint8_t Radio_AppIsReady(void)
{return s_state==RADIO_READY || s_state==RADIO_SEARCH;}
uint8_t Radio_AppIsSearching(void)
{return s_state==RADIO_SEARCH;}
void Radio_AppDraw(ui_t *ui)
{
    uint8_t fg;
    char text[28];
    if(!ui)return;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(0u,11u,s_audio_muted?"FM SEEK   MUTED":"FM SEEK  AUDIO ON");
    Disp_DrawLine(0u,14u,127u,14u);
    if(s_state==RADIO_ERROR)
    {
        (void)snprintf(text,sizeof(text),"I2C ERR %u",(unsigned)s_error_code);
        Disp_DrawStr(8u,36u,text);
    }
    else if(s_state==RADIO_RESET_WAIT || s_state==RADIO_POWER_WAIT ||
            s_state==RADIO_TUNE_WAIT)
        Disp_DrawStr(8u,36u,"Initializing...");
    else
    {
        (void)snprintf(text,sizeof(text),"%u.%u",
                       (unsigned)(s_freq_mhz10/10u),
                       (unsigned)(s_freq_mhz10%10u));
        Disp_SetFont(font_radio_h24w12);
        Disp_DrawStr(4u,40u,text);
        Disp_SetFont(UI_FONT);
        Disp_DrawStr(91u,38u,"MHz");
    }
    Disp_SetFont(UI_FONT);
    if(s_unmute_armed && (uint32_t)(Millis()-s_unmute_arm_at)<3000u)
        Disp_DrawStr(0u,61u,"LOUD! ENTER AGAIN");
    else if(s_state==RADIO_SEARCH)Disp_DrawStr(0u,61u,"Seeking...");
    else if(s_state==RADIO_READY)
    {
        if(s_search_failed)Disp_DrawStr(0u,61u,"No station");
        else
        {
            (void)snprintf(text,sizeof(text),"RSSI %u %s",
                           (unsigned)s_rssi,s_stereo?"ST":"MO");
            Disp_DrawStr(0u,61u,text);
        }
    }
    else Disp_DrawStr(0u,61u,"ENTER audio / BACK");
    Disp_SendBuffer();
}
void Radio_AppRun(ui_t *ui)
{
    if(!ui)return;
    if(ui->action==UI_ACTION_BACK)
    {
        Radio_AppStop();
        return; /* Preserve BACK so MiaoUI exits the WORD function page. */
    }
    if(s_state==RADIO_OFF)Radio_AppStart();
    else if(s_state==RADIO_READY)
    {
        if(ui->action==UI_ACTION_UP)radio_seek(0u);
        else if(ui->action==UI_ACTION_DOWN)radio_seek(1u);
        else if(ui->action==UI_ACTION_ENTER)Radio_AppToggleAudio();
    }
    /* MiaoUI WORD pages leave automatically on any unconsumed key. */
    ui->action=UI_ACTION_NONE;
    Radio_AppPoll();
    Radio_AppDraw(ui);
}
