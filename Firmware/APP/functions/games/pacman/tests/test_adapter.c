#include "../game_pacman.h"
#include "buttons.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static uint8_t buffer[1024];
static KeyState_t keys[5];
static uint32_t now, delay, frames, sound_calls;
uint32_t Millis(void){return now;}
KeyState_t KEY_GetState(KeyIndex_t k){return keys[k];}
uint8_t *Disp_GetBufferPtr(void){return buffer;}
void Disp_SendBuffer(void){frames++;}
void UI_RequestNextFrame(ui_t *ui,uint32_t next){(void)ui;delay=next;}
uint8_t beep_busy(void){return 0u;}
uint8_t beep_enqueue(const uint16_t *notes){(void)notes;sound_calls++;return 1u;}
int main(void){
    ui_t ui={UI_ACTION_NONE};
    uint8_t attract[1024], active[1024];
    now=200u;
    Game_PacmanRun(&ui);
    assert(frames==1u && delay==44u);
    memcpy(attract,buffer,1024u);
    assert(attract[0]==(uint8_t)~0u);
    ui.action=UI_ACTION_ENTER;
    now+=44u;
    Game_PacmanRun(&ui);
    assert(ui.action==UI_ACTION_NONE);
    memcpy(active,buffer,1024u);
    assert(memcmp(active,attract,1024u)!=0);
    /* CONF pressed is no toggle; release resumes. */
    keys[KeyIndex_CONF]=KeyState_ShortPress;
    now+=44u;
    Game_PacmanRun(&ui);
    memcpy(attract,buffer,1024u);
    now+=44u;
    Game_PacmanRun(&ui);
    assert(memcmp(attract,buffer,1024u)==0);
    keys[KeyIndex_CONF]=KeyState_None;
    now+=44u;
    Game_PacmanRun(&ui);
    /* STOP1 gap doesn't generate a simulation catch-up. */
    now+=120000u;
    Game_PacmanRun(&ui);
    assert(frames>=5u);
    ui.action=UI_ACTION_BACK;
    Game_PacmanRun(&ui);
    Game_PacmanOnLeave(&ui);
    ui.action=UI_ACTION_NONE;
    now+=44u;
    Game_PacmanRun(&ui);
    assert(frames>=7u && delay==44u);
    puts("native framebuffer, CONF hold, STOP gap, exit/re-enter PASS");
    return 0;
}
