/* PB10A temporary automatic smoke test. Only three checks:
 * 1. PA6 SUB_EN: enable USB-A and sample PA0, PA5 ADC raw counts.
 * 2. PB1 TIM3_CH4: 2 kHz PWM for 500 ms, listen for a beep.
 * 3. RDA5807: auto-seek once, intentionally MUTED until user presses ENTER
 *    in the Radio application (VOL=0 does not silence the headphones).
 *
 * No high-current USB load, charging protocol or full FM menu tests.
 * OLED is used for result observation, not for claiming measured PASS.
 * Disable PB10A_HW_TEST_AUTORUN after hardware verification.
 */
#include "hw_test.h"
#include "main.h"
#include "adc.h"
#include "beep_hw.h"
#include "millis.h"
#include "radio_app.h"
#include "ssd1306_display.h"
#include "display/dispDriver.h"
#include <stdio.h>

enum {HW_USB=0u, HW_BEEP=1u, HW_FM=2u, HW_DONE=3u};
static uint8_t s_active,s_phase,s_adc_started,s_beep_on;
static volatile uint8_t s_adc_ok;
static uint32_t s_phase_started,s_last_draw;
static uint16_t s_adc_samples[4] __attribute__((aligned(4)));
static uint8_t s_adc_error;
static void draw_status(void)
{
    char text[24];
    uint8_t fg=1u,bg=0u;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&bg);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    Disp_SetDrawColor(&fg);
    Disp_SetFont(UI_FONT);
    Disp_DrawStr(0u,11u,"PB10A BOARD TEST");
    Disp_DrawLine(0u,14u,127u,14u);
    if(s_phase==HW_USB)
    {
        Disp_DrawStr(0u,28u,"1 USB-A: PA6 ON");
        if(s_adc_ok)
        {
            (void)snprintf(text,sizeof(text),"PA0 ADC: %u",(unsigned)s_adc_samples[0]);
            Disp_DrawStr(0u,42u,text);
            (void)snprintf(text,sizeof(text),"PA5 ADC: %u",(unsigned)s_adc_samples[1]);
            Disp_DrawStr(0u,55u,text);
        }
        else Disp_DrawStr(0u,52u,s_adc_error?"ADC init failed":"Sampling ADC...");
    }
    else if(s_phase==HW_BEEP)
    {
        Disp_DrawStr(0u,31u,"2 BEEP: 2 kHz");
        Disp_DrawStr(0u,49u,"500 ms tone");
    }
    else if(s_phase==HW_FM)
    {
        Disp_DrawStr(0u,29u,"3 FM: auto seek");
        Disp_DrawStr(0u,45u,Radio_AppIsSearching()?"Seeking...":"Scanning status");
        Disp_DrawStr(0u,61u,"Audio MUTED");
    }
    Disp_SendBuffer();
}
void HW_TestInit(void)
{
    s_phase=HW_USB;
    s_active=1u;s_phase_started=Millis();
    s_last_draw=Millis()-500u;s_adc_error=0u;s_adc_ok=0u;
    s_adc_started=0u;s_beep_on=0u;
    LL_GPIO_SetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
    /* A 4-channel scan: CH0=PA0, CH5=PA5, temperature, VREFINT.
     * With software trigger and non-continuous mode one conversion scan
     * is sufficient for first-pass analog presence verification. */
    if(HAL_ADCEx_Calibration_Start(&hadc1)!=HAL_OK)
        s_adc_error=1u;
    else if(HAL_ADC_Start_DMA(&hadc1,(uint32_t *)(void *)s_adc_samples,4u)!=HAL_OK)
        s_adc_error=1u;
    else
        s_adc_started=1u;
}
uint8_t HW_TestActive(void){return s_active;}
void HW_TestPoll(void)
{
    uint32_t now=Millis();
    if(!s_active)return;
    SSD1306_Display_Service();
    if(s_phase==HW_USB)
    {
        if((uint32_t)(now-s_phase_started)>=PB10A_HW_TEST_USB_MS)
        {
            LL_GPIO_ResetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
            if(s_adc_started)(void)HAL_ADC_Stop_DMA(&hadc1);
            s_phase=HW_BEEP;s_phase_started=now;
            beep_hw_set_arr(499u); /* 64MHz/(63+1)=1MHz, 500 counts = 2kHz */
            s_beep_on=1u;
        }
    }
    else if(s_phase==HW_BEEP)
    {
        if((uint32_t)(now-s_phase_started)>=500u)
        {
            if(s_beep_on)beep_hw_stop();
            s_beep_on=0u;s_phase=HW_FM;s_phase_started=now;
#if PB10A_HW_TEST_FM
            Radio_AppStart(); /* deliberately hardware-muted even when VOL=0 */
#else
            s_phase=HW_DONE;
#endif
        }
    }
    else if(s_phase==HW_FM)
    {
#if PB10A_HW_TEST_FM
        Radio_AppPoll();
        if((uint32_t)(now-s_phase_started)>=PB10A_HW_TEST_FM_MS)
            s_phase=HW_DONE;
#else
        s_phase=HW_DONE;
#endif
    }
    if(s_phase==HW_DONE)
    {
        if(s_beep_on)beep_hw_stop();
        LL_GPIO_ResetOutputPin(SUB_EN_GPIO_Port,SUB_EN_Pin);
        s_active=0u;
        /* Control returns to the normal MiaoUI scheduler.
         * Radio remains muted and tuned until the user enters its app. */
        return;
    }
    if(SSD1306_Display_IsReady() && !SSD1306_Display_FrameBusy() &&
       (uint32_t)(now-s_last_draw)>=100u)
    {
        s_last_draw=now;
        draw_status();
    }
}

/* IRQ callback: only a completed four-channel sequence is reported as valid.
 * Circular DMA mode may issue subsequent completions; values remain a
 * first-pass analog observation, not a calibrated voltage/current result. */
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc && hadc->Instance==ADC1 && s_phase==HW_USB && s_active)
        s_adc_ok=1u;
}
