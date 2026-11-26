/*
 * TinyPacman by Daniel C, original GPL-3.0-or-later graphics and gameplay.
 * PB10A adapter: native 128x64 page bytes, 90-degree physical viewing,
 * native key holds / MiaoUI WORD / cooperative, non-blocking Beep Service.
 */
#include "game_pacman.h"
#include "pacman_core.h"
#include "pacman_assets.h"
#include "../game_shared.h"
#include "rle.h"
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
static uint8_t s_session, s_prev_dpad_mask;

static uint8_t pressed(KeyIndex_t k) {
    KeyState_t v = KEY_GetState(k);
    return (uint8_t)(v == KeyState_ShortPress || v == KeyState_LongPress);
}

/* Read debounced held states on every MiaoUI pass. Rising edges are one-shot:
 * an older held direction cannot overwrite a newer press. Last rising key wins
 * if multiple keys appear in the same pass. Do not use menu DAS repeats. */
static pacman_direction_t read_new_direction(void) {
    static const KeyIndex_t keys[4] = {
        KeyIndex_UP, KeyIndex_DOWN, KeyIndex_LEFT, KeyIndex_RIGHT
    };
    static const pacman_direction_t dirs[4] = {
        PACMAN_UP, PACMAN_DOWN, PACMAN_LEFT, PACMAN_RIGHT
    };
    uint8_t mask = 0u, new_presses;
    uint8_t i;
    pacman_direction_t latest = PACMAN_NONE;
    for (i = 0u; i < 4u; ++i)
        if (pressed(keys[i])) mask |= (uint8_t)(1u << i);
    new_presses = (uint8_t)(mask & (uint8_t)~s_prev_dpad_mask);
    s_prev_dpad_mask = mask;
    for (i = 0u; i < 4u; ++i)
        if (new_presses & (1u << i)) latest = dirs[i];
    return latest;
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

/* Original lives/fruit margin is x=0..7 in native screen coordinates,
 * which appears at the bottom when the board is turned 90 degrees CCW.
 * Draw a minimal 5x7 upright-in-portrait caption there only in READY.
 * Each 7-bit font column is mapped directly into the native page buffer;
 * the 1024-byte shared framebuffer is not rotated or duplicated. */
static const uint8_t *intro_glyph(char c) {
    static const uint8_t glyphs[8][5] = {
        {0x01u, 0x01u, 0x7fu, 0x01u, 0x01u}, /* T */
        {0x3fu, 0x40u, 0x40u, 0x40u, 0x3fu}, /* U */
        {0x7fu, 0x09u, 0x19u, 0x29u, 0x46u}, /* R */
        {0x7fu, 0x02u, 0x04u, 0x08u, 0x7fu}, /* N */
        {0x46u, 0x49u, 0x49u, 0x29u, 0x1fu}, /* 9 */
        {0x3eu, 0x45u, 0x49u, 0x51u, 0x3eu}, /* 0 */
        {0x3eu, 0x41u, 0x41u, 0x41u, 0x22u}, /* C */
        {0x3fu, 0x40u, 0x38u, 0x40u, 0x3fu}  /* W */
    };
    switch (c) {
    case 'T': return glyphs[0];
    case 'U': return glyphs[1];
    case 'R': return glyphs[2];
    case 'N': return glyphs[3];
    case '9': return glyphs[4];
    case '0': return glyphs[5];
    case 'C': return glyphs[6];
    case 'W': return glyphs[7];
    default: return 0;
    }
}

static void draw_intro_rotation_hint(uint8_t *buf) {
    static const char hint[] = "TURN 90 CCW";
    uint8_t page, native_x, u = 1u, i;
    for (page = 0u; page < 8u; ++page)
        for (native_x = 0u; native_x < 8u; ++native_x)
            buf[(uint16_t)page * 128u + native_x] = 0u;
    for (i = 0u; hint[i]; ++i) {
        const uint8_t *glyph = intro_glyph(hint[i]);
        uint8_t column, row;
        if (!glyph) { u = (uint8_t)(u + 4u); continue; }
        for (column = 0u; column < 5u; ++column) {
            uint8_t native_y = (uint8_t)(u + column);
            for (row = 0u; row < 7u; ++row) {
                if (glyph[column] & (1u << row)) {
                    native_x = (uint8_t)(7u - row);
                    buf[(uint16_t)(native_y >> 3) * 128u + native_x] |=
                        (uint8_t)(1u << (native_y & 7u));
                }
            }
        }
        u = (uint8_t)(u + 6u);
    }
}

/* u8g2 R0 128x64 full buffer is SSD1306 page-packed. The original game's
 * main image lives across x=0..127; users view it in portrait by turning
 * the physical board, not by applying u8g2 R1/R3 raster rotation. */
static void render(void) {
    uint8_t page, x;
    uint8_t *buf = Disp_GetBufferPtr();
    if (!buf) return;
    /* Decode display maze directly to existing OLED DMA framebuffer. Wall
     * collision stays in the separate shared SRAM asset cache. */
    if (RLE_Decode(pacman_BackBlitz_rle, sizeof(pacman_BackBlitz_rle),
                   buf, GAME_BUFFER_SIZE, RLE_WRITE) != RLE_OK) return;
    for (page = 0u; page < 8u; ++page) {
        for (x = 0u; x < 128u; ++x) {
            uint16_t pos = (uint16_t)page * 128u + x;
            uint8_t b = (uint8_t)(buf[pos] | draw_actors(x, page));
            if (s_game.phase == PACMAN_READY) {
                b = (uint8_t)~b; /* Original attract-mode inversion. */
            } else {
                b |= draw_lives(x, page);
                b |= draw_fruit(x, page);
            }
            buf[pos] = b;
        }
    }
    if (s_game.phase == PACMAN_READY) {
        draw_intro_rotation_hint(buf);
    } else {
        /* Original corner pills each occupy two adjacent bytes and blink
         * together; preserve all eight exact mask values/indices. */
        uint8_t i;
        uint8_t blink = (uint8_t)((s_game.frame >= 6u && s_game.frame <= 12u) ||
                                   (s_game.frame >= 18u && s_game.frame <= 24u));
        for (i = 0u; i < 64u; ++i) {
            uint16_t dot;
            uint8_t mask_index;
            if (!Pacman_CoreDotVisible(&s_game, i)) continue;
            if (blink && (i == 0u || i == 1u || i == 12u || i == 13u ||
                          i == 50u || i == 51u || i == 62u || i == 63u))
                continue;
            dot = pacman_dot_records[i];
            mask_index = (uint8_t)((dot >> 10) & 7u);
            buf[(((dot >> 7) & 7u) * 128u) + (dot & 127u)] |=
                pacman_dot_masks[mask_index];
        }
    }
    Disp_SendBuffer();
}

void Game_PacmanOnLeave(ui_t *ui) {
    (void)ui;
    if (s_session != GAME_SESSION_ACTIVE)
        return;
    s_session = GAME_SESSION_PAUSED;
    s_accum = 0u;
    s_prev_dpad_mask = 0u;
    Pacman_CoreClearQueuedDirection(&s_game);
    Pacman_CoreBindBack(0);
    GameBuffer_Release(GAME_OWNER_PACMAN);
}

static uint8_t load_collision_map(void) {
    uint8_t *buffer = GameBuffer_Acquire(GAME_OWNER_PACMAN);
    if (!buffer) return 0u;
    if (RLE_Decode(pacman_BackBlitz_rle, sizeof(pacman_BackBlitz_rle),
                   buffer, GAME_BUFFER_SIZE, RLE_WRITE) != RLE_OK ||
        RLE_Decode(pacman_back_xor_rle, sizeof(pacman_back_xor_rle),
                   buffer, GAME_BUFFER_SIZE, RLE_XOR) != RLE_OK) {
        GameBuffer_Release(GAME_OWNER_PACMAN);
        return 0u;
    }
    Pacman_CoreBindBack(buffer);
    return 1u;
}

void Game_PacmanRun(ui_t *ui) {
    uint32_t now, elapsed;
    uint8_t hold_confirm, confirm_released;
    uint8_t steps;
    pacman_direction_t new_direction;
    if (!ui) return;
    if (ui->action == UI_ACTION_BACK) {
        Pacman_CoreClearQueuedDirection(&s_game);
        return; /* MiaoUI owns long-CONF exit. */
    }
    now = Millis();
    if (s_session != GAME_SESSION_ACTIVE) {
        if (!load_collision_map()) {
            ui->action = UI_ACTION_NONE;
            UI_RequestNextFrame(ui, PACMAN_FRAME_MS);
            return;
        }
        if (s_session == GAME_SESSION_NEW)
            Pacman_CoreNewGame(&s_game, now);
        s_session = GAME_SESSION_ACTIVE;
        s_accum = 0u;
        s_last_time = now;
        s_prev_dpad_mask = 0u;
    }
    hold_confirm = pressed(KeyIndex_CONF);
    confirm_released = (uint8_t)(ui->action == UI_ACTION_ENTER);
    new_direction = read_new_direction();
    if (confirm_released &&
        (s_game.phase == PACMAN_READY || s_game.phase == PACMAN_GAME_OVER))
        Pacman_CoreStart(&s_game);
    if (hold_confirm || confirm_released) {
        /* CONF cancels the queued turn, even when a short release is all
         * that arrives in this UI frame. It never stops current movement. */
        Pacman_CoreClearQueuedDirection(&s_game);
    } else {
        Pacman_CoreQueueDirection(&s_game, new_direction);
    }
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
            Pacman_CoreTick(&s_game, PACMAN_NONE);
            s_accum -= PACMAN_LOGIC_MS;
        }
    } /* CONF held: preserve only sub-frame progress, no catch-up. */
    play_event(Pacman_CoreTakeSound(&s_game));
    render();
    UI_RequestNextFrame(ui, PACMAN_FRAME_MS);
}
