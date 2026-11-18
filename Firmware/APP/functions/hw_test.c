/* PB10A temporary boot foreground: FM auto-seek only.
 * PA6 SUB_EN and PA0/PA5 sampling are managed separately by PowerADC.
 * Reuse the actual Collar-Radio-style Radio_AppDraw, not a second test UI.
 */
#include "hw_test.h"
#include "radio_app.h"
#include "ssd1306_display.h"
#include "millis.h"

static uint8_t s_active;
static uint32_t s_started;

void HW_TestInit(void)
{
    s_active=1u;
    s_started=Millis();
    Radio_AppStart();
}
uint8_t HW_TestActive(void){return s_active;}
void HW_TestPoll(ui_t *ui)
{
    uint32_t now=Millis();
    if(!s_active)return;
    SSD1306_Display_Service();
    Radio_AppPoll();
    if(SSD1306_Display_IsReady() && !SSD1306_Display_FrameBusy())
        Radio_AppDraw(ui);
    if((uint32_t)(now-s_started)>=PB10A_HW_TEST_FM_MS &&
       !Radio_AppIsSearching())
        s_active=0u; /* Do not truncate an ongoing seek. */
    /* Radio remains active until its ordinary menu callback stops it. */
}
