#include "../game_pacman.h"
#include "../game_pacman_core.h"
#include "buttons.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
const uint8_t font_home_h6w4[] = {0}, font_radio_h12w6[] = {0};
static uint32_t time_now, frame_delay;
static uint32_t frames;
void UI_RequestNextFrame(ui_t *ui,uint32_t delay){(void)ui;frame_delay=delay;}
uint32_t Millis(void){return time_now;}
KeyState_t KEY_GetState(KeyIndex_t index){(void)index;return KeyState_None;}
void Disp_SetMaxClipWindow(void){}
void Disp_ClearBuffer(void){}
void Disp_SetDrawColor(void *p){(void)p;}
void Disp_DrawBox(uint16_t x,uint16_t y,uint16_t w,uint16_t h){(void)x;(void)y;(void)w;(void)h;}
void Disp_DrawFrame(uint16_t x,uint16_t y,uint16_t w,uint16_t h){(void)x;(void)y;(void)w;(void)h;}
void Disp_DrawDisc(uint16_t x,uint16_t y,uint16_t r,uint8_t op){(void)x;(void)y;(void)r;(void)op;}
void Disp_DrawPixel(uint16_t x,uint16_t y){assert(x<128 && y<64);}
void Disp_SetFont(const uint8_t *font){(void)font;}
uint16_t Disp_DrawStr(uint16_t x,uint16_t y,const char *text){(void)x;(void)y;(void)text;return 0;}
void Disp_SendBuffer(void){++frames;}
int main(void){
    ui_t u={UI_ACTION_NONE};
    time_now=0u;
    Game_PacmanRun(&u);
    assert(frames==1u && (frame_delay == 33u || frame_delay == 34u));
    assert(PacmanGame_Get()->phase==PACMAN_READY);
    u.action=UI_ACTION_ENTER;
    time_now=33u;
    Game_PacmanRun(&u);
    assert(PacmanGame_Get()->phase==PACMAN_PLAYING);
    u.action=UI_ACTION_BACK;
    time_now=66u;
    Game_PacmanRun(&u);
    assert(PacmanGame_Get()->pellets_left==0u);
    Game_PacmanRun(&u); /* BACK must not start a new session. */
    assert(PacmanGame_Get()->pellets_left == 0u);
    u.action=UI_ACTION_NONE;
    Game_PacmanRun(&u);
    assert(PacmanGame_Get()->lives==3u);
    Game_PacmanOnLeave(&u);
    assert(PacmanGame_Get()->lives == 0u && PacmanGame_Get()->pellets_left == 0u);
    puts("pacman_app: MiaoUI lifecycle and RAM cleanup PASS");
    return 0;
}
