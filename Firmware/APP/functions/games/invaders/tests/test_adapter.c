#include "../game_invaders.h"
#include "../game_shared.h"
#include "buttons.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint8_t pixels[1024];
static KeyState_t keys[5];
static uint32_t now, frames, delay, notes;
uint32_t Millis(void){return now;}
KeyState_t KEY_GetState(KeyIndex_t k){return keys[k];}
uint8_t *Disp_GetBufferPtr(void){return pixels;}
void Disp_SendBuffer(void){++frames;}
void UI_RequestNextFrame(ui_t *ui,uint32_t next){(void)ui;delay=next;}
uint8_t beep_busy(void){return 0u;}
uint8_t beep_enqueue(const uint16_t *sequence){(void)sequence;++notes;return 1u;}
int main(void) {
    ui_t ui={UI_ACTION_NONE};
    uint8_t title[1024], frozen[1024];
    now=100u;Game_InvadersRun(&ui);
    memcpy(title,pixels,sizeof(title));
    assert(frames==1u && delay==44u);
    assert(GameBuffer_Owner()==GAME_OWNER_INVADERS);
    ui.action=UI_ACTION_ENTER;
    now+=44u;Game_InvadersRun(&ui);
    assert(ui.action==UI_ACTION_NONE && memcmp(pixels,title,1024u)!=0);
    keys[KeyIndex_CONF]=KeyState_ShortPress;
    now+=44u;Game_InvadersRun(&ui);
    memcpy(frozen,pixels,1024u);
    now+=44u;Game_InvadersRun(&ui);
    assert(memcmp(frozen,pixels,1024u)==0);
    keys[KeyIndex_CONF]=KeyState_None;
    now+=44u;Game_InvadersRun(&ui);
    now+=120000u;Game_InvadersRun(&ui); /* No accelerated STOP1 catch-up. */
    ui.action=UI_ACTION_BACK;Game_InvadersRun(&ui);
    {
        uint8_t *cached = GameBuffer_Acquire(GAME_OWNER_INVADERS);
        assert(cached != 0);
        Game_InvadersOnLeave(&ui);
        assert(GameBuffer_Owner() == GAME_OWNER_NONE);
        for (unsigned i=0u;i<1024u;i++) assert(cached[i]==0u);
    }
    now+=44u;ui.action=UI_ACTION_NONE;Game_InvadersRun(&ui);
    assert(memcmp(pixels,title,1024u)==0 && frames>=6u);
    puts("Tiny Invaders adapter: landscape, CONF pause, long BACK, STOP1/restart PASS");
    return 0;
}
