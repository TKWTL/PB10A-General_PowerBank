/* Tiny Bomber by Daniel C (2018-2021), GPL-3.0-or-later.
 * PB10A adapter: horizontal native SSD1306 page output and MiaoUI WORD.
 * No AVR runtime, extra display buffer, blocking audio or idle inhibition. */
#include "game_bomber.h"
#include "bomber_core.h"
#include "bomber_assets.h"
#include "../game_shared.h"
#include "rle.h"
#include "display/dispDriver.h"
#include "core/ui.h"
#include "buttons.h"
#include "millis.h"
#include "beep_service.h"
#include <stdint.h>

#define BOMBER_LOGIC_MS 22u
#define BOMBER_RENDER_MS 44u
#define BOMBER_WAKE_GAP_MS 150u

static bomber_game_t s_game;
static uint32_t s_previous_ms, s_elapsed_ms;
static uint8_t s_session, s_place_pending;

static uint8_t pressed(KeyIndex_t key) {
    KeyState_t state = KEY_GetState(key);
    return (uint8_t)(state == KeyState_ShortPress || state == KeyState_LongPress);
}

static bomber_direction_t direction(void) {
    if (pressed(KeyIndex_UP)) return BOMBER_UP;
    if (pressed(KeyIndex_DOWN)) return BOMBER_DOWN;
    if (pressed(KeyIndex_LEFT)) return BOMBER_LEFT;
    if (pressed(KeyIndex_RIGHT)) return BOMBER_RIGHT;
    return BOMBER_NONE;
}

/* Lower priority than an existing system sound; playback itself is driven
 * by the existing 1 ms Beep Service, not by game code. */
static const uint16_t s_start_sound[] = {BEEP_C5, 70u, BEEP_E5, 70u, BEEP_END};
static const uint16_t s_place_sound[] = {BEEP_A5, 50u, BEEP_END};
static const uint16_t s_blast_sound[] = {BEEP_G5, 90u, BEEP_C5, 90u, BEEP_END};
static const uint16_t s_death_sound[] = {BEEP_E5, 100u, BEEP_C5, 160u, BEEP_END};
static const uint16_t s_level_sound[] = {BEEP_G5, 110u, BEEP_C6, 100u, BEEP_END};

static void play_event(bomber_sound_t s) {
    const uint16_t *notes = 0;
    if (s == BOMBER_SOUND_NONE || beep_busy()) return;
    switch (s) {
    case BOMBER_SOUND_START: notes = s_start_sound; break;
    case BOMBER_SOUND_PLACE: notes = s_place_sound; break;
    case BOMBER_SOUND_BLAST: notes = s_blast_sound; break;
    case BOMBER_SOUND_DEATH: notes = s_death_sound; break;
    case BOMBER_SOUND_LEVEL: notes = s_level_sound; break;
    default: break;
    }
    if (notes) (void)beep_enqueue(notes);
}

/* A sprite spans at most two adjacent native SSD1306 pages. */
static uint8_t actor_byte(const bomber_actor_t *a, uint8_t x, uint8_t page) {
    uint16_t index;
    uint8_t glyph;
    if (a->dead || (int16_t)x < a->x || (int16_t)x >= a->x + 8) return 0u;
    index = (uint16_t)((int16_t)x - a->x) + (uint16_t)a->type * 96u +
            (uint16_t)a->anim * 8u + (uint16_t)a->direction_anim * 8u;
    if (index >= sizeof(bomber_caracters)) return 0u;
    glyph = bomber_caracters[index];
    if (a->page_y == (int8_t)page) return (uint8_t)(glyph << a->offset_y);
    if (a->page_y + 1 == (int8_t)page && a->offset_y)
        return (uint8_t)(glyph >> (8u - a->offset_y));
    return 0u;
}

static uint8_t actors_byte(uint8_t x, uint8_t page) {
    uint8_t i, combined = 0u;
    for (i = 0u; i < 5u; i++)
        combined |= actor_byte(&s_game.actors[i], x, page);
    return combined;
}

/* Original BlocDetect: FF=HUD, FE=cell 1, otherwise directly a column.
 * Original 14-byte, MSB-first map remains the sole mutable block state. */
static uint8_t block_byte(uint8_t x, uint8_t page) {
    uint8_t col;
    if (page == 0u) return 0u;
    col = bomber_BlocDetect[x];
    if (col == 0xffu) return 0u;
    if (col == 0xfeu) col = 1u;
    if (!Bomber_CoreBlockVisible(&s_game,
         (int16_t)col + ((int16_t)page - 1) * 15)) return 0u;
    return (uint8_t)((x & 1u) ? 0x55u : 0xaau);
}

static uint8_t bomb_byte(uint8_t x, uint8_t page) {
    const bomber_bomb_t *b = &s_game.bomb;
    if (!b->fuse || b->anim > 2u ||
        b->page_y != page || (int16_t)x < b->x || (int16_t)x >= b->x + 8)
        return 0u;
    return bomber_bomb[(uint16_t)b->anim * 8u + (uint16_t)((int16_t)x - b->x)];
}

/* Existing 24x24 fire art, repeated to a 40x40 cross. Clamp every
 * drawn cell to the SAME wall-aware reach mask as CoreTick's damage. */
static uint8_t fire_byte(uint8_t x, uint8_t page) {
    const bomber_bomb_t *b = &s_game.bomb;
    int16_t left = b->x - (int16_t)BOMBER_FIRE_RADIUS_CELLS * 8;
    int16_t top = b->page_y - (int16_t)BOMBER_FIRE_RADIUS_CELLS;
    int16_t col = (int16_t)x - left, row = (int16_t)page - top;
    uint8_t sx, sy;
    uint16_t offset;
    if (b->fuse != 1u || b->anim > 2u || x <= 7u || page == 0u ||
        col < 0 || row < 0 ||
        col >= (int16_t)(2u * BOMBER_FIRE_RADIUS_CELLS + 1u) * 8 ||
        row >= (int16_t)(2u * BOMBER_FIRE_RADIUS_CELLS + 1u))
        return 0u;
    if (!Bomber_CoreFireCell(&s_game,
          (int8_t)(col / 8 - (int16_t)BOMBER_FIRE_RADIUS_CELLS),
          (int8_t)(row - (int16_t)BOMBER_FIRE_RADIUS_CELLS))) return 0u;
    sx = (uint8_t)(col / 8 < (int16_t)BOMBER_FIRE_RADIUS_CELLS ? 0u :
         (col / 8 == (int16_t)BOMBER_FIRE_RADIUS_CELLS ? 8u : 16u));
    sx = (uint8_t)(sx + (col & 7));
    sy = row < (int16_t)BOMBER_FIRE_RADIUS_CELLS ? 0u :
         (row == (int16_t)BOMBER_FIRE_RADIUS_CELLS ? 1u : 2u);
    offset = (uint16_t)sy * 72u + (uint16_t)b->anim * 24u + sx;
    return offset < sizeof(bomber_fire) ? bomber_fire[offset] : 0u;
}

static uint8_t lives_mask(uint8_t x, uint8_t page) {
    if (x < 1u || x > 7u) return 0xffu;
    if ((page == 4u && s_game.lives < 3u) ||
        (page == 5u && s_game.lives < 2u) ||
        (page == 6u && s_game.lives < 1u)) return 0u;
    return 0xffu;
}

static void render(void) {
    uint8_t page, x;
    uint8_t *buffer = Disp_GetBufferPtr();
    if (!buffer) return;
    if (RLE_Decode(bomber_BackBlitz_rle, sizeof(bomber_BackBlitz_rle),
                   buffer, GAME_BUFFER_SIZE, RLE_WRITE) != RLE_OK) return;
    if (s_game.phase != BOMBER_READY && s_game.phase != BOMBER_GAME_OVER) {
        for (page = 0u; page < 8u; ++page) {
            for (x = 0u; x < 128u; ++x) {
                uint16_t pos = (uint16_t)page * 128u + x;
                buffer[pos] = (uint8_t)(lives_mask(x, page) &
                    (buffer[pos] | actors_byte(x, page) | block_byte(x, page) |
                     bomb_byte(x, page) | fire_byte(x, page)));
            }
        }
    }
    Disp_SendBuffer();
}

void Game_BomberOnLeave(ui_t *ui) {
    (void)ui;
    if (s_session != GAME_SESSION_ACTIVE)
        return;
    s_session = GAME_SESSION_PAUSED;
    s_place_pending = 0u;
    s_elapsed_ms = 0u;
    Bomber_CoreBindBack(0);
    GameBuffer_Release(GAME_OWNER_BOMBER);
}

static uint8_t load_collision_map(void) {
    uint8_t *buffer = GameBuffer_Acquire(GAME_OWNER_BOMBER);
    if (!buffer) return 0u;
    if (RLE_Decode(bomber_back_rle, sizeof(bomber_back_rle),
                   buffer, GAME_BUFFER_SIZE, RLE_WRITE) != RLE_OK) {
        GameBuffer_Release(GAME_OWNER_BOMBER);
        return 0u;
    }
    Bomber_CoreBindBack(buffer);
    return 1u;
}

void Game_BomberRun(ui_t *ui) {
    uint8_t holding_confirm, ticks;
    uint32_t now, dt;
    if (!ui) return;
    if (ui->action == UI_ACTION_BACK) {
        s_place_pending = 0u;
        return; /* MiaoUI restores its own BACK action and exits WORD page. */
    }
    now = Millis();
    if (s_session != GAME_SESSION_ACTIVE) {
        if (!load_collision_map()) {
            ui->action = UI_ACTION_NONE;
            UI_RequestNextFrame(ui, BOMBER_RENDER_MS);
            return;
        }
        if (s_session == GAME_SESSION_NEW)
            Bomber_CoreNewGame(&s_game, now);
        s_session = GAME_SESSION_ACTIVE;
        s_elapsed_ms = 0u;
        s_place_pending = 0u;
        s_previous_ms = now;
    }
    holding_confirm = pressed(KeyIndex_CONF);
    if (ui->action == UI_ACTION_ENTER) {
        if (s_game.phase == BOMBER_READY || s_game.phase == BOMBER_GAME_OVER) {
            Bomber_CoreStart(&s_game);
            s_place_pending = 0u;
        } else if (s_game.phase == BOMBER_PLAYING && !holding_confirm) {
            s_place_pending = 1u; /* CONF short release: exactly one bomb. */
        }
    }
    ui->action = UI_ACTION_NONE; /* Consume D-pad and CONF short menu actions. */
    dt = now - s_previous_ms;
    s_previous_ms = now;
    if (dt > BOMBER_WAKE_GAP_MS) {
        /* STOP1 or OLED suspension: never advance the fuse off-screen. */
        s_elapsed_ms = 0u;
    } else if (!holding_confirm) {
        s_elapsed_ms += dt;
        for (ticks = 0u; s_elapsed_ms >= BOMBER_LOGIC_MS && ticks < 3u; ticks++) {
            Bomber_CoreTick(&s_game, direction(), s_place_pending);
            s_place_pending = 0u;
            s_elapsed_ms -= BOMBER_LOGIC_MS;
        }
    } /* Hold CONF => freeze logical time and the bomb fuse. */
    if (s_game.phase != BOMBER_PLAYING) s_place_pending = 0u;
    play_event(Bomber_CoreTakeSound(&s_game));
    render();
    UI_RequestNextFrame(ui, BOMBER_RENDER_MS);
}
