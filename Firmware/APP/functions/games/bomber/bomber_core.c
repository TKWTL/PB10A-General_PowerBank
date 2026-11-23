/*
 * Tiny Bomber, Daniel C 2018-2021, GPL-3.0-or-later.
 * PB10A rewrite: AVR busy loops/goto replaced with 22 ms cooperative steps.
 * Keep native 6x7 collision masks, 15x7 bit-map and four random-turn enemies.
 */
#include "bomber_core.h"
#include "bomber_assets.h"
#include <string.h>

static const uint8_t *s_back_map;
/* Caller owns and maintains one pre-decompressed fixed-wall bitmap. */
void Bomber_CoreBindBack(const uint8_t *map) { s_back_map = map; }

#define BOMBER_STOP 2u
#define BOMBER_MAP_CELLS 105
#define BOMBER_ENEMIES 4u

static uint32_t next_random(bomber_game_t *g) {
    uint32_t v = g->random_state;
    if (!v) v = 0x6d2b79f5u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    g->random_state = v;
    return v;
}

static uint8_t back_byte(int16_t x, int16_t page) {
    if (x < 0 || x >= 128 || page < 0 || page >= 8) return 0u;
    return s_back_map ? s_back_map[(uint16_t)page * 128u + (uint16_t)x] : 0u;
}

uint8_t Bomber_CoreBlockVisible(const bomber_game_t *g, int16_t index) {
    if (index < 0 || index >= BOMBER_MAP_CELLS) return 0u;
    return (uint8_t)((g->blocks[(uint16_t)index >> 3] &
                       (0x80u >> ((uint16_t)index & 7u))) != 0u);
}

static void clear_block(bomber_game_t *g, int16_t index) {
    if (index < 0 || index >= BOMBER_MAP_CELLS) return;
    g->blocks[(uint16_t)index >> 3] &=
        (uint8_t)~(0x80u >> ((uint16_t)index & 7u));
}

/* Original CheckCollisionWithBlock SKIPS both FF and FE sentinels.
 * At x=16, FE aliases column 1 for rendering only; the 6px-wide actor
 * collision probe must not count the adjacent block. Otherwise the player
 * at x=9 cannot traverse the empty leftmost lane on the first map.
 * Keep FE->1 conversion ONLY in game_bomber.c's block_byte renderer. */
static int16_t column_at(int16_t x) {
    uint8_t value;
    if (x < 0 || x >= 128) return -1;
    value = bomber_BlocDetect[x];
    return (value == 0xffu || value == 0xfeu) ? -1 : (int16_t)value;
}

static uint8_t block_collision(const bomber_game_t *g, const bomber_actor_t *p) {
    int16_t px = p->x, py = (int16_t)p->page_y * 8 + p->offset_y;
    int16_t left = column_at(px), right = column_at(px + 7);
    int16_t top = py / 8 - 1, bottom = (py + 7) / 8 - 1;
    if (left >= 0 && (Bomber_CoreBlockVisible(g, left + top * 15) ||
                      Bomber_CoreBlockVisible(g, left + bottom * 15))) return 1u;
    if (right >= 0 && (Bomber_CoreBlockVisible(g, right + top * 15) ||
                       Bomber_CoreBlockVisible(g, right + bottom * 15))) return 1u;
    return 0u;
}

static uint8_t overlaps(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                        int16_t bx, int16_t by, int16_t bw, int16_t bh) {
    return (uint8_t)!(ax + aw < bx || ay + ah < by ||
                      ax > bx + bw || ay > by + bh);
}

/* Bombs block enemies, never the player (original CheckCollisionWithBomb). */
static uint8_t bomb_collision(const bomber_game_t *g, uint8_t i) {
    const bomber_actor_t *p = &g->actors[i];
    if (i == 0u || !g->bomb.fuse) return 0u;
    return overlaps(p->x, (int16_t)p->page_y * 8 + p->offset_y, 7, 7,
                    g->bomb.x, g->bomb.page_y * 8, 7, 7);
}

uint8_t Bomber_CoreCollisionBack(const bomber_game_t *g, uint8_t i,
                                 uint8_t horizontal) {
    const bomber_actor_t *p;
    int16_t x, page, py;
    uint8_t mask, next, k;
    if (i > BOMBER_ENEMIES) return 1u;
    p = &g->actors[i];
    x = p->x;
    page = p->page_y;
    py = page * 8 + p->offset_y;
    /* Prevent negative AVR indexing and off-screen actor coordinates. */
    if (x < 8 || x + 6 >= 128 || py < 8 || py + 6 >= 64) return 1u;
    if (horizontal) { /* Original HorVcheck=1, horizontal x movement. */
        if (p->dir_v != BOMBER_STOP) {
            int16_t check_x = x + (p->dir_v == 1u ? 6 : 0);
            mask = (uint8_t)(0x7fu << p->offset_y);
            next = p->offset_y ? (uint8_t)(0x7fu >> (8u - p->offset_y)) : 0u;
            if ((back_byte(check_x, page) & mask) ||
                (back_byte(check_x, page + 1) & next)) return 1u;
        }
    } else if (p->dir_h != BOMBER_STOP) {
        uint8_t shifted = (uint8_t)(p->dir_h == 1u && p->offset_y > 2u);
        int16_t check_page = page + shifted;
        mask = shifted ? (uint8_t)(0x7fu >> (8u - p->offset_y)) :
                         (uint8_t)(0x7fu << p->offset_y);
        for (k = 0u; k <= 6u; k++)
            if (back_byte(x + k, check_page) & mask) return 1u;
    }
    return (uint8_t)(block_collision(g, p) || bomb_collision(g, i));
}

static void reset_bomb(bomber_game_t *g) {
    g->bomb.x = -10;
    g->bomb.page_y = -10;
    g->bomb.fuse = 0u;
    g->bomb.anim = 0u;
}

static void reset_positions(bomber_game_t *g) {
    static const uint8_t xs[5] = {9u, 56u, 120u, 119u, 24u};
    static const int8_t ys[5] = {7, 1, 2, 7, 5};
    uint8_t i;
    reset_bomb(g);
    for (i = 0u; i < 5u; ++i) {
        bomber_actor_t *p = &g->actors[i];
        p->type = (uint8_t)(i != 0u);
        p->x = xs[i];
        p->page_y = ys[i];
        p->offset_y = 0u;
        if (!i) {
            p->dir_v = p->dir_h = BOMBER_STOP;
            p->direction_anim = 0u;
            p->dead = 0u;
            p->anim = p->switch_anim = 0u;
        } else {
            /* Original RESTARTLEVEL preserves the surviving enemy direction. */
            p->direction_anim = 0u;
        }
    }
}

static void load_level(bomber_game_t *g) {
    uint8_t i;
    memcpy(g->blocks, &bomber_level[(uint16_t)g->level * 14u], 14u);
    for (i = 1u; i < 5u; i++) {
        g->actors[i].dead = 0u;
        g->actors[i].dir_h = g->actors[i].dir_v = 0u;
        g->actors[i].anim = 0u;
    }
    reset_positions(g);
}

void Bomber_CoreNewGame(bomber_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->random_state = seed ? seed : 0x6d2b79f5u;
    g->lives = 3u;
    g->level = 0u;
    g->phase = BOMBER_READY;
    load_level(g);
}

void Bomber_CoreStart(bomber_game_t *g) {
    if (g->phase == BOMBER_GAME_OVER) {
        uint32_t seed = next_random(g);
        Bomber_CoreNewGame(g, seed);
    }
    if (g->phase == BOMBER_READY) {
        g->phase = BOMBER_PLAYING;
        g->sound_event = BOMBER_SOUND_START;
    }
}

uint8_t Bomber_CoreAllEnemiesDead(const bomber_game_t *g) {
    uint8_t i;
    for (i = 1u; i < 5u; i++) if (!g->actors[i].dead) return 0u;
    return 1u;
}

static uint8_t touching_enemy(const bomber_game_t *g) {
    uint8_t i;
    const bomber_actor_t *p = &g->actors[0];
    int16_t y = (int16_t)p->page_y * 8 + p->offset_y;
    for (i = 1u; i < 5u; i++) {
        const bomber_actor_t *q = &g->actors[i];
        if (!q->dead && overlaps(p->x, y, 6, 6, q->x,
                                 (int16_t)q->page_y * 8 + q->offset_y, 6, 6))
            return 1u;
    }
    return 0u;
}

static void input_player(bomber_game_t *g, bomber_direction_t input) {
    bomber_actor_t *p = &g->actors[0];
    switch (input) {
    case BOMBER_UP: p->dir_h = 0u; p->dir_v = BOMBER_STOP; break;
    case BOMBER_DOWN: p->dir_h = 1u; p->dir_v = BOMBER_STOP; break;
    case BOMBER_LEFT: p->dir_v = 0u; p->dir_h = BOMBER_STOP; break;
    case BOMBER_RIGHT: p->dir_v = 1u; p->dir_h = BOMBER_STOP; break;
    default:
        /* Original joystick stops each axis at an 8-pixel alignment. */
        if ((p->x & 7) == 0) p->dir_v = BOMBER_STOP;
        if (p->offset_y == 0u) p->dir_h = BOMBER_STOP;
        break;
    }
}

static void move_actors(bomber_game_t *g) {
    uint8_t i;
    for (i = 0u; i < 5u; i++) {
        bomber_actor_t *p = &g->actors[i];
        int16_t old_x;
        int8_t old_page;
        uint8_t old_offset;
        if (p->dead || ((g->frame & 1u) && (i == 0u || !g->speed_bomber)))
            continue;
        old_x = p->x;
        old_page = p->page_y;
        old_offset = p->offset_y;
        if (p->dir_v == 1u) ++p->x;
        else if (p->dir_v == 0u) --p->x;
        if (Bomber_CoreCollisionBack(g, i, 1u)) {
            p->dir_v = i ? (uint8_t)(next_random(g) & 1u) : BOMBER_STOP;
            p->x = old_x;
        }
        if (p->dir_h == 1u) {
            if (p->offset_y < 7u) ++p->offset_y;
            else {p->offset_y = 0u; ++p->page_y;}
        } else if (p->dir_h == 0u) {
            if (p->offset_y) --p->offset_y;
            else {p->offset_y = 7u; --p->page_y;}
        }
        if (Bomber_CoreCollisionBack(g, i, 0u)) {
            p->dir_h = i ? (uint8_t)(next_random(g) & 1u) : BOMBER_STOP;
            p->page_y = old_page;
            p->offset_y = old_offset;
        }
        if (i == 0u) {
            if (p->dir_h == 1u) p->direction_anim = 0u;
            if (p->dir_h == 0u) p->direction_anim = 6u;
            if (p->dir_v == 1u) p->direction_anim = 9u;
            if (p->dir_v == 0u) p->direction_anim = 3u;
            if (!p->switch_anim) {
                if (p->anim < 2u && (p->dir_h != BOMBER_STOP ||
                                    p->dir_v != BOMBER_STOP)) ++p->anim;
                else p->switch_anim = 1u;
            } else if (p->anim) --p->anim;
            else p->switch_anim = 0u;
        } else if (g->frame == 0u || g->frame == 12u) {
            p->direction_anim = 0u;
        }
        if (i && g->frame % 6u == 0u)
            p->anim = (uint8_t)((p->anim + 1u) % 3u);
    }
}

/* 40x40 cross: center plus 2 full 8px cells per cardinal direction.
 * No blast propagation or fixed-wall destruction; map bounds are clipped. */
static void destroy_blocks(bomber_game_t *g) {
    int16_t col = (g->bomb.x - 8) / 8;
    int16_t row = g->bomb.page_y - 1;
    int16_t delta;
    if (col < 0 || col >= 15 || row < 0 || row >= 7) return;
    for (delta = -2; delta <= 2; ++delta) {
        if (row + delta >= 0 && row + delta < 7)
            clear_block(g, col + (row + delta) * 15);
        if (col + delta >= 0 && col + delta < 15)
            clear_block(g, col + delta + row * 15);
    }
}

static void blast_damage(bomber_game_t *g) {
    uint8_t i;
    int16_t bx = g->bomb.x, by = g->bomb.page_y * 8;
    /* Expanded to a 40x40 cross (two extra 8px arms per direction).
     * Retain the original 7x7 actor hit boxes and inclusive edges.
     * Visual fire_byte() uses the same 40x40 clipping envelope. */
    for (i = 0u; i < 5u; i++) {
        bomber_actor_t *p = &g->actors[i];
        int16_t py = (int16_t)p->page_y * 8 + p->offset_y;
        if (p->dead) continue;
        if (overlaps(p->x, py, 7, 7, bx + 1, by - 14, 6, 37) ||
            overlaps(p->x, py, 7, 7, bx - 14, by + 1, 37, 6)) {
            p->dead = 1u;
            p->page_y = 0;
            p->offset_y = 0u;
        }
    }
    destroy_blocks(g);
    g->sound_event = BOMBER_SOUND_BLAST;
}

static void next_level(bomber_game_t *g) {
    if (g->level < 2u) ++g->level;
    else {g->level = 0u; g->speed_bomber = 1u;}
    load_level(g);
    g->phase = BOMBER_PLAYING;
}

static void player_died(bomber_game_t *g) {
    g->sound_event = BOMBER_SOUND_DEATH;
    if (g->lives > 1u) {
        --g->lives;
        g->phase = BOMBER_DYING;
        g->delay_ticks = 18u;
    } else {
        g->lives = 0u;
        g->phase = BOMBER_GAME_OVER;
    }
}

void Bomber_CoreTick(bomber_game_t *g, bomber_direction_t input, uint8_t place) {
    if (g->phase == BOMBER_DYING || g->phase == BOMBER_CLEARED) {
        if (g->delay_ticks) --g->delay_ticks;
        if (!g->delay_ticks) {
            if (g->phase == BOMBER_DYING) {
                reset_positions(g); /* Preserve destroyed blocks and dead enemies. */
                g->phase = BOMBER_PLAYING;
            } else {
                next_level(g);
                g->sound_event = BOMBER_SOUND_LEVEL;
            }
        }
        return;
    }
    if (g->phase != BOMBER_PLAYING) return;
    input_player(g, input);
    if (place && !g->bomb.fuse) {
        const bomber_actor_t *p = &g->actors[0];
        g->bomb.x = ((p->x + 3) / 8) * 8;
        g->bomb.page_y = ((int16_t)p->page_y * 8 + p->offset_y + 3) / 8;
        g->bomb.fuse = 5u;
        g->bomb.anim = 0u;
        g->sound_event = BOMBER_SOUND_PLACE;
    }
    g->frame = (uint8_t)(g->frame < 24u ? g->frame + 1u : 0u);
    if (touching_enemy(g)) {player_died(g); return;}
    move_actors(g);
    /* Match original fuse stages: 0/8/16 and fire stage every four ticks. */
    if ((g->frame == 0u || g->frame == 8u || g->frame == 16u) &&
        g->bomb.fuse > 1u) {
        if (g->bomb.anim < 2u) ++g->bomb.anim;
        else {g->bomb.anim = 0u; --g->bomb.fuse;}
    }
    if (g->frame % 4u == 0u && g->bomb.fuse == 1u) {
        if (g->bomb.anim < 2u) {
            if (g->bomb.anim == 1u) blast_damage(g);
            ++g->bomb.anim;
        } else reset_bomb(g);
    }
    if (g->actors[0].dead || touching_enemy(g)) {
        player_died(g);
        return;
    }
    if (g->frame == 12u && Bomber_CoreAllEnemiesDead(g)) {
        g->phase = BOMBER_CLEARED;
        g->delay_ticks = 36u;
        g->sound_event = BOMBER_SOUND_LEVEL;
    }
}

bomber_sound_t Bomber_CoreTakeSound(bomber_game_t *g) {
    bomber_sound_t s = (bomber_sound_t)g->sound_event;
    g->sound_event = BOMBER_SOUND_NONE;
    return s;
}
