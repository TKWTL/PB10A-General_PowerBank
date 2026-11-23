#include "../game_bomber.h"
#include "../bomber_assets.h"
#include "buttons.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t framebuffer[1024];
static KeyState_t keys[5];
static uint32_t now_ms, frame_delay, frames, sounds;
static const uint16_t *last_beep;
uint32_t Millis(void){return now_ms;}
KeyState_t KEY_GetState(KeyIndex_t k){return keys[k];}
uint8_t *Disp_GetBufferPtr(void){return framebuffer;}
void Disp_SendBuffer(void){++frames;}
void UI_RequestNextFrame(ui_t *ui,uint32_t next){(void)ui;frame_delay=next;}
uint8_t beep_busy(void){return 0u;}
uint8_t beep_enqueue(const uint16_t *sequence){last_beep=sequence;++sounds;return 1u;}

int main(void) {
    ui_t ui={UI_ACTION_NONE};
    uint8_t title[1024], playing[1024];
    now_ms = 200u;
    Game_BomberRun(&ui);
    assert(frames==1u && frame_delay==44u);
    memcpy(title,framebuffer,1024u);
    assert(memcmp(title,bomber_BackBlitz,1024u)==0);
    ui.action=UI_ACTION_ENTER;
    now_ms+=44u;
    Game_BomberRun(&ui);
    assert(ui.action == UI_ACTION_NONE && sounds>=1u && last_beep!=0);
    memcpy(playing,framebuffer,1024u);
    assert(memcmp(playing,title,1024u)!=0);
    /* Hold CONF freezes the original renderer and all logical frame ticks. */
    keys[KeyIndex_CONF]=KeyState_ShortPress;
    now_ms+=44u;
    Game_BomberRun(&ui);
    memcpy(playing,framebuffer,1024u);
    now_ms+=44u;
    Game_BomberRun(&ui);
    assert(memcmp(playing,framebuffer,1024u)==0);
    keys[KeyIndex_CONF]=KeyState_None;
    ui.action=UI_ACTION_ENTER; /* One short release => request one bomb. */
    now_ms+=44u;
    Game_BomberRun(&ui);
    assert(ui.action==UI_ACTION_NONE);
    now_ms+=44u;
    Game_BomberRun(&ui);
    /* No fast-forward after a long STOP1 gap. */
    memcpy(playing,framebuffer,1024u);
    now_ms+=600000u;
    Game_BomberRun(&ui);
    assert(memcmp(playing,framebuffer,1024u)==0);
    assert(frames>=6u && frame_delay==44u);
    keys[KeyIndex_CONF]=KeyState_LongPress;
    ui.action=UI_ACTION_BACK;
    uint32_t old_frames=frames;
    Game_BomberRun(&ui);
    assert(frames==old_frames && ui.action==UI_ACTION_BACK);
    Game_BomberOnLeave(&ui);
    keys[KeyIndex_CONF]=KeyState_None;
    ui.action=UI_ACTION_NONE;
    now_ms+=44u;
    Game_BomberRun(&ui);
    assert(frames==old_frames+1u);
    assert(memcmp(title,framebuffer,1024u)==0);
    puts("bomber_adapter: frame buffer, CONF pause/release/BACK, STOP1 PASS");
    return 0;
}
