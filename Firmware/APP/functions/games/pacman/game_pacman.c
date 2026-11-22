/*
 * TinyPacman by Daniel C, original GPL-3.0-or-later graphics and gameplay.
 * PB10A adapter: native 128x64 page bytes, 90-degree physical viewing,
 * native key holds / MiaoUI WORD / cooperative, non-blocking Beep Service.
 */
#include "game_pacman.h"
#include "pacman_core.h"
#include "pacman_assets.h"
#include "display/dispDriver.h"
#include "core/ui.h"
#include "buttons.h"
#include "millis.h"
#include "beep_service.h"
#include <stdint.h>

#define PACMAN_LOGIC_MS 22u
#define PACMAN_FRAME_MS 44u
#define PACMAN_WAKE_GAP_MS 150u

static pacman_game_t s_game;  /* all mutable game state, no heap or NVM */
static uint32_t s_last_time, s_accum;
static uint8_t s_active;

static uint8_t pressed(KeyIndex_t k) {
    KeyState_t v = KEY_GetState(k);
    return (uint8_t)(v == KeyState_ShortPress || v == KeyState_LongPress);
}

/* Viewed after turning the whole PB10A 90 degrees counterclockwise. */
static pacman_direction_t read_direction(void) {
    if (pressed(KeyIndex_RIGHT)) return PACMAN_UP;
    if (pressed(KeyIndex_LEFT)) return PACMAN_DOWN;
    if (pressed(KeyIndex_UP)) return PACMAN_LEFT;
    if (pressed(KeyIndex_DOWN)) return PACMAN_RIGHT;
    return PACMAN_NONE;
}

/* Keep service-owned sound short, non-blocking and lowest priority. Skip
 * game sounds if another application already owns the shared buzzer. */
static const uint16_t s_start[] = {BEEP_C5, 55u, BEEP_E5, 55u, BEEP_END};
static const uint16_t s_dot[] = {BEEP_E5, 20u, BEEP_END};
static const uint16_t s_power[] = {BEEP_A5, 90u, BEEP_END};
static const uint16_t s_ghost[] = {BEEP_G5, 70u, BEEP_C6, 70u, BEEP_END};
static const uint16_t s_death[] = {BEEP_E5, 110u, BEEP_C5, 140u, BEEP_END};
static const uint16_t s_level[] = {BEEP_G5, 85u, BEEP_C6, 110u, BEEP_END};

static void play_event(pacman_sound_t event) {
    const uint16_t *sequence = 0;
    if (event == PACMAN_SOUND_NONE || beep_busy()) return;
    switch (event) {
    case PACMAN_SOUND_START: sequence = s_start; break;
    case PACMAN_SOUND_DOT: sequence = s_dot; break;
    case PACMAN_SOUND_POWER: sequence = s_power; break;
    case PACMAN_SOUND_GHOST: sequence = s_ghost; break;
    case PACMAN_SOUND_DEATH: sequence = s_death; break;
    case PACMAN_SOUND_LEVEL: sequence = s_level; break;
    default: break;
    }
    if (sequence) (void)beep_enqueue(sequence);
}

static uint8_t character_byte(const pacman_actor_t *p, uint8_t actor,
                              uint8_t x, uint8_t page) {
    uint16_t offset;
    uint8_t sprite;
    if (x < p->x || (uint16_t)x >= (uint16_t)p->x + 8u) return 0u;
    if (actor == 0u && s_game.phase == PACMAN_READY) return 0u;
    offset = (uint16_t)(x - p->x) + (uint16_t)p->type * 96u +
             (uint16_t)p->anim * 8u + (uint16_t)p->direction_anim * 8u;
    if (actor != 0u) {
        if (p->guber) offset += 64u;
        else if (s_game.power_active &&
                 (((s_game.frame >= 6u && s_game.frame <= 12u) ||
                   (s_game.frame >= 18u && s_game.frame <= 24u)) ||
                   s_game.power_timer > s_game.gobbing_end)) offset += 32u;
    }
    if (offset >= sizeof(pacman_caracters)) return 0u;
    sprite = pacman_caracters[offset];
    if (p->page_y == (int8_t)page) return (uint8_t)(sprite << p->offset_y);
    if (p->page_y + 1 == (int8_t)page && p->offset_y)
        return (uint8_t)(sprite >> (8u - p->offset_y));
    return 0u;
}

static uint8_t draw_actors(uint8_t x, uint8_t page) {
    uint8_t i, v = 0u;
    for (i = 0u; i < 5u; i++) v |= character_byte(&s_game.actors[i], i, x, page);
    return v;
}

static uint8_t draw_dot(uint8_t x, uint8_t page, uint8_t *count) {
    uint8_t v = pacman_dots[(uint16_t)page * 128u + x];
    uint8_t special;
    if (!v) return 0u;
    special = (uint8_t)(*count == 0u || *count == 1u || *count == 12u ||
                        *count == 13u || *count == 50u || *count == 51u ||
                        *count == 62u || *count == 63u);
    if (!Pacman_CoreDotVisible(&s_game, *count)) v = 0u;
    if (special && ((s_game.frame >= 6u && s_game.frame <= 12u) ||
                    (s_game.frame >= 18u && s_game.frame <= 24u))) v = 0u;
    ++*count;
    return v;
}

static uint8_t draw_lives(uint8_t x, uint8_t page) {
    return (uint8_t)((page < s_game.lives && x < 8u) ?
        pacman_caracters[x + 8u] : 0u);
}
static uint8_t draw_fruit(uint8_t x, uint8_t page) {
    uint8_t frame;
    if (x >= 8u || page < 4u || page > 7u) return 0u;
    frame = (uint8_t)(7u - page);
    if (frame != 0u && s_game.level_speed > 200u - 10u * frame) return 0u;
    return pacman_fruits[(uint16_t)frame * 8u + x];
}

/* u8g2 R0 128x64 full buffer is SSD1306 page-packed. The original game's
 * main image lives across x=0..127; users view it in portrait by turning
 * the physical board, not by applying u8g2 R1/R3 raster rotation. */
static void render(void) {
    uint8_t page, x, dot_index = 0u;
    uint8_t *buf = Disp_GetBufferPtr();
    if (!buf) return;
    for (page = 0u; page < 8u; page++) {
        for (x = 0u; x < 128u; x++) {
            uint16_t pos = (uint16_t)page * 128u + x;
            uint8_t b = (uint8_t)(pacman_BackBlitz[pos] |
                                  draw_actors(x, page));
            if (s_game.phase == PACMAN_READY) {
                b = (uint8_t)~b; /* Original attract-mode inversion. */
            } else {
                b |= draw_dot(x, page, &dot_index);
                b |= draw_lives(x, page);
                b |= draw_fruit(x, page);
            }
            buf[pos] = b;
        }
    }
    Disp_SendBuffer();
}

void Game_PacmanOnLeave(ui_t *ui) {
    (void)ui;
    s_active = 0u; /* Re-entry calls NewGame; no NVM and no pointless memset. */
    s_accum = 0u;
}

void Game_PacmanRun(ui_t *ui) {
    uint32_t now, elapsed;
    uint8_t hold_confirm;
    uint8_t steps;
    if (!ui) return;
    if (ui->action == UI_ACTION_BACK) return; /* MiaoUI owns long-CONF exit. */
    now = Millis();
    if (!s_active) {
        Pacman_CoreNewGame(&s_game, now);
        s_active = 1u;
        s_accum = 0u;
        s_last_time = now;
    }
    hold_confirm = pressed(KeyIndex_CONF);
    if (ui->action == UI_ACTION_ENTER &&
        (s_game.phase == PACMAN_READY || s_game.phase == PACMAN_GAME_OVER))
        Pacman_CoreStart(&s_game);
    ui->action = UI_ACTION_NONE; /* Consume menu direction and CONF release. */

    elapsed = (uint32_t)(now - s_last_time);
    s_last_time = now;
    if (elapsed > PACMAN_WAKE_GAP_MS) {
        /* STOP1, display-off or long DMA suspension: discard elapsed time.
         * This freezes game frames and remaining power-pill time. */
        s_accum = 0u;
    } else if (!hold_confirm) {
        s_accum += elapsed;
        for (steps = 0u; s_accum >= PACMAN_LOGIC_MS && steps < 3u; steps++) {
            Pacman_CoreTick(&s_game, read_direction());
            s_accum -= PACMAN_LOGIC_MS;
        }
    } /* CONF held: preserve only sub-frame progress, no catch-up. */
    play_event(Pacman_CoreTakeSound(&s_game));
    render();
    UI_RequestNextFrame(ui, PACMAN_FRAME_MS);
}
