#include "../game_pacman.c"
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
    /* Portrait-upright text T starts at native (x=7,y=1); stripe is cleared.
     * Other x=8+ columns keep the original inverted attract image. */
    assert((attract[7] & 0x02u) != 0u);
    assert(attract[0] == 0u);
    {
        uint8_t base[1024];
        assert(RLE_Decode(pacman_BackBlitz_rle,sizeof(pacman_BackBlitz_rle),
                          base,sizeof(base),RLE_WRITE)==RLE_OK);
        assert(attract[8] == (uint8_t)~base[8]);
    }
    assert(GameBuffer_Owner() == GAME_OWNER_PACMAN);
    /* The glyphs and 90-degree hint remain strictly within x<8. */
    for (unsigned page=0u;page<8u;page++) {
        assert((attract[page*128u+0u] == 0u));
    }
    ui.action=UI_ACTION_ENTER;
    now+=44u;
    Game_PacmanRun(&ui);
    assert(ui.action==UI_ACTION_NONE);
    assert(s_game.phase==PACMAN_PLAYING);
    memcpy(active,buffer,1024u);
    assert(memcmp(active,attract,1024u)!=0);
    /* One-shot directional edges; last newly pressed button supersedes the
     * older held button, and release must not clear an unexecuted request. */
    s_accum=0u;
    keys[KeyIndex_UP]=KeyState_ShortPress;
    now+=1u;
    Game_PacmanRun(&ui); /* 1 ms: input captured, no logic step */
    assert(s_game.queued_direction==PACMAN_UP);
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_UP);
    keys[KeyIndex_LEFT]=KeyState_ShortPress;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_LEFT);
    keys[KeyIndex_LEFT]=KeyState_Release;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_LEFT);
    keys[KeyIndex_RIGHT]=KeyState_ShortPress;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_RIGHT);
    keys[KeyIndex_CONF]=KeyState_ShortPress;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_NONE);
    /* While CONF is held, new direction edges are ignored. */
    keys[KeyIndex_DOWN]=KeyState_ShortPress;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_NONE);
    keys[KeyIndex_UP]=keys[KeyIndex_RIGHT]=keys[KeyIndex_DOWN]=KeyState_None;
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
    /* A short-release ENTER also clears the one-slot pending input. */
    Pacman_CoreQueueDirection(&s_game,PACMAN_UP);
    assert(s_game.queued_direction==PACMAN_UP);
    ui.action=UI_ACTION_ENTER;
    now+=1u;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_NONE);
    /* STOP1 gap doesn't generate a simulation catch-up. */
    now+=120000u;
    Game_PacmanRun(&ui);
    assert(frames>=5u);
    Pacman_CoreQueueDirection(&s_game,PACMAN_DOWN);
    assert(s_game.queued_direction==PACMAN_DOWN);
    ui.action=UI_ACTION_BACK;
    Game_PacmanRun(&ui);
    assert(s_game.queued_direction==PACMAN_NONE);
    {
        uint8_t *cache = GameBuffer_Acquire(GAME_OWNER_PACMAN);
        assert(cache != 0);
        Game_PacmanOnLeave(&ui);
        assert(GameBuffer_Owner() == GAME_OWNER_NONE);
        for (unsigned i=0;i<1024u;i++) assert(cache[i] == 0u);
    }
    ui.action=UI_ACTION_NONE;
    now+=44u;
    Game_PacmanRun(&ui);
    assert(frames>=7u && delay==44u);
    assert(s_game.queued_direction==PACMAN_NONE);
    assert((buffer[7] & 0x02u)!=0u); /* READY hint reappears. */
    puts("native framebuffer, CONF hold, STOP gap, exit/re-enter PASS");
    return 0;
}
