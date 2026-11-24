/* Faithful Collar-Radio I2C Scanner visual port:
 * - 1500ms splash using the 24px Spleen title font (I2C / Scanner)
 * - 128x64 address matrix, 16 columns x 8 rows of 4px glyphs
 * - one address grid position reveals each UI frame (including --),
 *   probing only valid 7-bit I2C addresses without blocking other tasks
 * - automatic scan again after 5 seconds, ENTER immediate rescan
 * - CONF long/BACK or any directional key exits MiaoUI WORD application
 *
 * Only the safe 7-bit addresses 0x08..0x77 are probed; reserved
 * positions remain "--". This preserves the original 16x8 layout.
 */
#include "i2c_scanner.h"
#include "core/ui.h"
#include "millis.h"
#include "i2c.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "pm_api.h"
#include "mini_format.h"
#define SCANNER_SPLASH_MS 1500u
#define SCANNER_RESCAN_MS 5000u
#define SCANNER_FIRST 0x08u
#define SCANNER_LAST  0x77u
#define SCANNER_SLEEP_BLOCK 0x02u
enum {SCANNER_TITLE=0,SCANNER_SCAN,SCANNER_HOLD};
static uint8_t s_active,s_phase,s_next,s_ack[128];
static uint8_t s_probe_addr,s_probe_pending;
static volatile uint8_t s_probe_flag; /* IRQ-written completion state */
static uint32_t s_phase_at;
static void scanner_start(uint32_t now,uint8_t title) {
    uint8_t i;
    for(i=0;i<128u;i++)s_ack[i]=0u;
    s_next=0u; /* reveal the original 128-cell grid from 0x00 onward */
    s_phase=title?SCANNER_TITLE:SCANNER_SCAN;
    s_phase_at=now;
}

static void scanner_clear(ui_t *ui) {
    uint8_t fg;
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&ui->bgColor);
    Disp_DrawBox(0u,0u,UI_HOR_RES,UI_VER_RES);
    fg=(uint8_t)(ui->bgColor^1u);
    Disp_SetDrawColor(&fg);
}

void i2c_Scanner(ui_t *ui) {
    uint8_t addr;
    char buf[4];
    uint32_t now=Millis();
    if(!ui)return;
    if(ui->action!=UI_ACTION_NONE && ui->action!=UI_ACTION_ENTER) {
        /* Physical LEFT/RIGHT/UP/DOWN map to UP/DOWN/MINUS/PLUS
         * on a MiaoUI ICON WORD page. All four exit like long CONF. */
        s_active=0u;
        pm_api_set_sleep_block(SCANNER_SLEEP_BLOCK,0u);
        ui->action=UI_ACTION_BACK;
        return;
    }
    if(!s_active) {
        s_active=1u;
        pm_api_set_sleep_block(SCANNER_SLEEP_BLOCK,1u);
        scanner_start(now,1u);
    }
    else if(ui->action==UI_ACTION_ENTER)
        scanner_start(now,0u);
    /* MiaoUI WORD pages exit on any unconsumed action. */
    ui->action=UI_ACTION_NONE;

    if(s_phase==SCANNER_TITLE &&
        (uint32_t)(now-s_phase_at)>=SCANNER_SPLASH_MS) {
        s_phase=SCANNER_SCAN;
        s_phase_at=now;
    }
    if(s_probe_pending && s_probe_flag!=I2C_ASYNC_PENDING) {
        /* A former scan may finish after re-entering or pressing CONF:
         * never apply a completion belonging to a previous address. */
        if(s_phase==SCANNER_SCAN && s_next==s_probe_addr) {
            s_ack[s_next]=(uint8_t)(s_probe_flag==I2C_ASYNC_DONE);
            ++s_next;
        }
        s_probe_pending=0u;
    }
    if(s_phase==SCANNER_SCAN && !s_probe_pending) {
        /* Exactly one address grid cell advances per visible frame.
         * Only safe addresses cause an asynchronous I2C transaction. */
        if(s_next<128u) {
            if(s_next<SCANNER_FIRST || s_next>SCANNER_LAST) ++s_next;
            else if(I2C_AsyncProbe((uint16_t)s_next<<1,&s_probe_flag)) {
                s_probe_addr=s_next;
                s_probe_pending=1u;
            }
        }
        if(s_next>=128u) {
            s_phase=SCANNER_HOLD;
            s_phase_at=now;
        }
    }
    else if(s_phase==SCANNER_HOLD &&
        (uint32_t)(now-s_phase_at)>=SCANNER_RESCAN_MS)
    scanner_start(now,0u);

    /* Only the active scan needs one cell per 20 ms UI frame.
     * Splash and hold can sleep until the next state transition; keys
     * still wake the UI independently of this requested interval. */
    if(s_phase==SCANNER_TITLE)
        UI_RequestNextFrame(ui, SCANNER_SPLASH_MS - (uint32_t)(now-s_phase_at));
    else if(s_phase==SCANNER_HOLD)
        UI_RequestNextFrame(ui, SCANNER_RESCAN_MS - (uint32_t)(now-s_phase_at));

    scanner_clear(ui);
    if(s_phase==SCANNER_TITLE) {
        Disp_SetFont(font_radio_h24w12);
        Disp_DrawStr(17u,24u,"I2C");
        Disp_DrawStr(17u,50u,"Scanner");
    }
    else {
        Disp_SetFont(font_home_h6w4);
        /* Unscanned cells remain blank; -- appears progressively
         * as each position has been processed, Collar-Radio style. */
        for(addr=0u;addr<s_next;addr++) {
            if(s_ack[addr]) (void)mini_snprintf(buf,sizeof(buf),"%02x",(unsigned)addr);
            else {buf[0]='-';buf[1]='-';buf[2]='\0';}
            Disp_DrawStr((uint16_t)(8u*(addr%16u)),
                (uint16_t)(8u*(addr/16u+1u)),buf);
        }
    }
    Disp_SendBuffer();
}
