/* PB10A FM bring-up (fixed search mode). Derived from the operational
 * flow in Collar-Radio Application/Applications/radio.c, retaining its
 * RDA5807 driver register model. No nested while(1) and no FreeRTOS APIs.
 *
 * Enter: wake radio, initialize and start ONE seek automatically.
 * UP/DOWN: seek lower/higher; BACK: power off. No extra settings.
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
#include "time_service.h"
#include <stdio.h>

enum { RADIO_OFF=0, RADIO_RESET_WAIT, RADIO_POWER_WAIT, RADIO_TUNE_WAIT,
       RADIO_SEARCH, RADIO_READY, RADIO_ERROR };
#define RADIO_PM_BLOCK 0x04u
#define RADIO_QUERY_MS 180u
#define RADIO_SEEK_TIMEOUT_MS 8000u
#define RADIO_DEFAULT_VOLUME 0u /* existing lowest register gain, not mute */
static uint8_t s_state, s_first_seek_pending, s_search_failed;
static uint8_t s_volume, s_stereo, s_rssi;
/* Collar-Radio radio.c: original XBM status icons (7x9, 6x9, 16x8). */
static const uint8_t s_signal_icon[]={
    0x7f,0x49,0x2a,0x1c,0x08,0x08,0x08,0x08,0x08
};
static const uint8_t s_speaker_icon[]={
    0x30,0x28,0x27,0x25,0x25,0x25,0x27,0x28,0x30
};
static const uint8_t s_battery_icon[]={
    0xfc,0xff,0x04,0x80,0x07,0x80,0x05,0x80,0x05,0x80,
    0x07,0x80,0x04,0x80,0xfc,0xff
};
#define RADIO_MAX_STATIONS 50u
static uint16_t s_stations[RADIO_MAX_STATIONS]; /* units: 25 kHz */
static uint8_t s_station_count;
static uint16_t s_freq_units; /* 25 kHz units, as in Collar-Radio */
static uint32_t s_state_at, s_last_query, s_last_rtc;
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
static void radio_save_station(uint16_t freq)
{
    uint8_t i,j;
    if(s_station_count>=RADIO_MAX_STATIONS)return;
    for(i=0u;i<s_station_count && s_stations[i]<freq;i++){}
    if(i<s_station_count && s_stations[i]==freq)return;
    for(j=s_station_count;j>i;j--)s_stations[j]=s_stations[j-1u];
    s_stations[i]=freq;
    ++s_station_count;
}
/* Original x-axis maps 86.0-108.0 MHz to an approximately 0-127 marker. */
static uint8_t radio_marker_x(uint16_t frequency)
{
    uint32_t x;
    if(frequency<=3440u)return 0u;
    x=((uint32_t)(frequency-3440u)*5u)/36u;
    return x>127u?127u:(uint8_t)x;
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
    s_volume=RADIO_DEFAULT_VOLUME;s_stereo=0u;s_rssi=0u;
    s_freq_units=3480u;s_station_count=0u;
    s_last_rtc=Millis();
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
    s_first_seek_pending=1u;
    s_state=RADIO_OFF;
}
void Radio_AppPoll(void)
{
    uint32_t now=Millis();
    uint16_t status=0u, signal=0u;
    if((uint32_t)(now-s_last_rtc)>=1000u)
    {
        s_last_rtc=now;
        RTC_UpdateTime();
    }
    if(s_state==RADIO_RESET_WAIT)
    {
        if((uint32_t)(now-s_state_at)<50u)return;
        Register[0]|=(RDA5807_REG02_ENABLE|RDA5807_REG02_DMUTE|
                      RDA5807_REG02_DHIZ|RDA5807_REG02_SEEKUP);
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
    s_freq_units=(uint16_t)(3480u+4u*(status&RDA5807_REG0A_CHAN_MSK));
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
            if(!s_search_failed)radio_save_station(s_freq_units);
            s_state=RADIO_READY;
        }
        else if((uint32_t)(now-s_state_at)>RADIO_SEEK_TIMEOUT_MS)
            radio_fail(6u);
    }
}
uint8_t Radio_AppIsReady(void)
{return s_state==RADIO_READY || s_state==RADIO_SEARCH;}
uint8_t Radio_AppIsSearching(void)
{return s_state==RADIO_SEARCH;}
/* Collar-Radio Radio_Run() visual structure, kept in its original positions:
 * top signal/headphone/volume/battery status, 24px centre frequency,
 * 43/45/47px dial and station markers, RTC time/date at bottom.
 * Only unknown battery fill is omitted until a battery telemetry service exists.
 * This is a single-pass MiaoUI callback, not Collar's blocking while(1).
 */
void Radio_AppDraw(ui_t *ui)
{
    uint8_t fg, i, x;
    char buf[24];
    uint16_t fraction, textwidth;
    if(!ui)return;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(font_radio_h12w6);

    /* Collar original XBM status widgets. MIC_INDET is PB10A jack detect. */
    Disp_DrawXBMP(1u,1u,7u,9u,s_signal_icon);
    (void)snprintf(buf,sizeof(buf),":%u",(unsigned)s_rssi);
    Disp_DrawStr(8u,10u,buf);
    Disp_DrawStr(48u,10u,
                 LL_GPIO_IsInputPinSet(MIC_INDET_GPIO_Port,MIC_INDET_Pin)?"OFF":"ON");
    Disp_DrawXBMP(69u,1u,6u,9u,s_speaker_icon);
    (void)snprintf(buf,sizeof(buf),":%u%%",
                   (unsigned)(((uint16_t)s_volume+1u)*100u/16u));
    Disp_DrawStr(75u,10u,buf);
    Disp_DrawXBMP(111u,2u,16u,8u,s_battery_icon);

    /* Same large Spleen digits as the original Collar-Radio UI. */
    Disp_SetFont(font_radio_h24w12);
    if(s_state==RADIO_SEARCH)
        Disp_DrawStr(4u,37u,"Seeking...");
    else if(s_state==RADIO_RESET_WAIT || s_state==RADIO_POWER_WAIT ||
            s_state==RADIO_TUNE_WAIT)
        Disp_DrawStr(10u,37u,"Tuning...");
    else if(s_state==RADIO_ERROR)
    {
        Disp_SetFont(font_radio_h12w6);
        (void)snprintf(buf,sizeof(buf),"I2C ERROR %u",(unsigned)s_error_code);
        Disp_DrawStr(21u,36u,buf);
    }
    else
    {
        fraction=(uint16_t)((s_freq_units%40u)*25u); /* kHz */
        if((fraction%10u)==0u)
            (void)snprintf(buf,sizeof(buf),"%u.%02u",
                           (unsigned)(s_freq_units/40u),
                           (unsigned)(fraction/10u));
        else
            (void)snprintf(buf,sizeof(buf),"%u.%03u",
                           (unsigned)(s_freq_units/40u),(unsigned)fraction);
        textwidth=Disp_GetUTF8Width(buf);
        x=(textwidth>=122u)?2u:(uint8_t)((122u-textwidth)/2u);
        Disp_DrawStr(x,38u,buf);
        Disp_SetFont(font_radio_h12w6);
        Disp_DrawStr(103u,38u,"MHz");
    }

    /* Original station ruler: x = (freq-3440)*5/36, at y=43-48. */
    x=radio_marker_x(s_freq_units);
    Disp_DrawPixel(x,43u);
    Disp_DrawLine(0u,45u,127u,45u);
    for(i=0u;i<s_station_count;i++)
    {
        uint8_t dot_x=radio_marker_x(s_stations[i]);
        Disp_DrawPixel(dot_x,47u);
        if(s_stations[i]==s_freq_units)
        {
            Disp_DrawPixel(dot_x,46u);
            Disp_DrawPixel(dot_x,48u);
        }
    }

    Disp_SetFont(font_radio_h12w6);
    (void)snprintf(buf,sizeof(buf),"%02d:%02d:%02d",
                   sTime.Hours,sTime.Minutes,sTime.Seconds);
    Disp_DrawStr(1u,63u,buf);
    (void)snprintf(buf,sizeof(buf),"20%02d/%02d/%02d",
                   sTime.Year,sTime.Month,sTime.Date);
    Disp_DrawStr(68u,63u,buf);
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
        if(ui->action==UI_ACTION_MINUS)radio_seek(0u);
        else if(ui->action==UI_ACTION_PLUS)radio_seek(1u);
    }
    /* MiaoUI WORD pages leave automatically on any unconsumed key. */
    ui->action=UI_ACTION_NONE;
    Radio_AppPoll();
    Radio_AppDraw(ui);
}
