/* Tiny Invaders v3.1 by Daniel C, GPL-3.0-or-later.
 * PB10A native landscape MiaoUI adapter; never advances game in render(). */
#include "game_invaders.h"
#include "invaders_core.h"
#include "invaders_assets.h"
#include "../game_shared.h"
#include "rle.h"
#include "display/dispDriver.h"
#include "core/ui.h"
#include "buttons.h"
#include "millis.h"
#include "beep_service.h"
#include <stdint.h>

#define INVADERS_LOGIC_MS 33u
#define INVADERS_FRAME_MS 44u
#define INVADERS_WAKE_GAP_MS 150u

static invaders_game_t s_game;
static uint32_t s_last_ms, s_accum;
static uint8_t s_session;

static uint8_t held(KeyIndex_t key) {
    KeyState_t state = KEY_GetState(key);
    return (uint8_t)(state == KeyState_ShortPress || state == KeyState_LongPress);
}

static invaders_move_t movement(void) {
    if (held(KeyIndex_LEFT)) return INVADERS_MOVE_LEFT;
    if (held(KeyIndex_RIGHT)) return INVADERS_MOVE_RIGHT;
    return INVADERS_NO_MOVE;
}

static const uint16_t s_start[] = {BEEP_C5,60u,BEEP_E5,70u,BEEP_END};
static const uint16_t s_shot[] = {BEEP_A5,20u,BEEP_END};
static const uint16_t s_hit[] = {BEEP_G5,35u,BEEP_END};
static const uint16_t s_die[] = {BEEP_E5,90u,BEEP_C5,140u,BEEP_END};
static const uint16_t s_level[] = {BEEP_G5,70u,BEEP_C6,90u,BEEP_END};
static const uint16_t s_ufo[] = {BEEP_C6,75u,BEEP_END};

static void play(invaders_sound_t event) {
    const uint16_t *notes = 0;
    if (beep_busy()) return;
    switch (event) {
    case INVADERS_SOUND_START: notes = s_start; break;
    case INVADERS_SOUND_SHOT: notes = s_shot; break;
    case INVADERS_SOUND_HIT: notes = s_hit; break;
    case INVADERS_SOUND_DIE: notes = s_die; break;
    case INVADERS_SOUND_LEVEL: notes = s_level; break;
    case INVADERS_SOUND_UFO: notes = s_ufo; break;
    default: break;
    }
    if (notes) (void)beep_enqueue(notes);
}

void Game_InvadersOnLeave(ui_t *ui) {
    (void)ui;
    if (s_session != GAME_SESSION_ACTIVE)
        return;
    s_session = GAME_SESSION_PAUSED;
    s_accum = 0u;
    Invaders_CoreBindBackground(0);
    GameBuffer_Release(GAME_OWNER_INVADERS);
}

static uint8_t load_background(void) {
    uint8_t *buffer = GameBuffer_Acquire(GAME_OWNER_INVADERS);
    if (!buffer) return 0u;
    if (RLE_Decode(invaders_back_rle, sizeof(invaders_back_rle),
                   buffer, GAME_BUFFER_SIZE, RLE_WRITE) != RLE_OK) {
        GameBuffer_Release(GAME_OWNER_INVADERS);
        return 0u;
    }
    Invaders_CoreBindBackground(buffer);
    return 1u;
}

void Game_InvadersRun(ui_t *ui) {
    uint32_t now, elapsed;
    uint8_t pause, steps;
    uint8_t *buffer;
    if (!ui) return;
    if (ui->action == UI_ACTION_BACK) return; /* MiaoUI long-CONF exit. */
    now = Millis();
    if (s_session != GAME_SESSION_ACTIVE) {
        if (!load_background()) {
            ui->action = UI_ACTION_NONE;
            UI_RequestNextFrame(ui, INVADERS_FRAME_MS);
            return;
        }
        if (s_session == GAME_SESSION_NEW)
            Invaders_CoreInit(&s_game, now);
        s_session = GAME_SESSION_ACTIVE;
        s_accum = 0u;
        s_last_ms = now;
    }
    pause = held(KeyIndex_CONF);
    if (ui->action == UI_ACTION_ENTER &&
        (s_game.phase == INVADERS_TITLE || s_game.phase == INVADERS_GAME_OVER))
        Invaders_CoreStart(&s_game);
    ui->action = UI_ACTION_NONE;
    elapsed = (uint32_t)(now - s_last_ms);
    s_last_ms = now;
    if (elapsed > INVADERS_WAKE_GAP_MS) s_accum = 0u;
    else if (!pause) {
        s_accum += elapsed;
        for (steps = 0u; s_accum >= INVADERS_LOGIC_MS && steps < 3u; ++steps) {
            Invaders_CoreTick(&s_game, movement(), held(KeyIndex_UP));
            s_accum -= INVADERS_LOGIC_MS;
        }
    }
    play(Invaders_CoreTakeSound(&s_game));
    buffer = Disp_GetBufferPtr();
    if (buffer) {
        Invaders_CoreRender(&s_game, buffer);
        Disp_SendBuffer();
    }
    UI_RequestNextFrame(ui, INVADERS_FRAME_MS);
}
