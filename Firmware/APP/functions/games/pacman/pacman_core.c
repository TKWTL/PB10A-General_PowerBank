/*
 * TinyPacman (Tinyjoypad ATTINY85) by Daniel C (2018-2021).
 * GPL-3.0-or-later, complete license in LICENSE-GPL-3.0.
 * PB10A port: preserve the original page-mask maze, 6x7 collision tests,
 * four random-turn ghosts, power timing, animation and level-speed policy.
 * Remove Arduino polling, blocking tone/delay, and uninitialized sprites.
 */
#include "pacman_core.h"
#include "pacman_assets.h"
#include <string.h>

#define PACMAN_ACTORS 5u
#define PACMAN_DOTS 64u
#define PACMAN_PLAYER 0u
#define PACMAN_GHOST 1u
#define PACMAN_DIR_STOP 2u

static uint32_t next_random(pacman_game_t *g) {
    uint32_t v = g->random_state;
    if (!v) v = 0x6d2b79f5u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    return (g->random_state = v);
}

static uint8_t back_byte(int16_t x, int16_t page) {
    if (x < 0 || x >= 128 || page < 0 || page >= 8) return 0;
    return pacman_back[(uint16_t)page * 128u + (uint16_t)x];
}

/* Original CheckCollisionWithBack/RecupeBacktoCompV/H/Trim masks. */
uint8_t Pacman_CoreCollisionBack(const pacman_actor_t *p, uint8_t horizontal) {
    int16_t x = p->x, page = p->page_y;
    uint8_t mask, i, other;
    if (horizontal) { /* H-axis movement, original HorVcheck==1. */
        if (p->dir_v == PACMAN_DIR_STOP) return 0u;
        x += (p->dir_v == 1u ? 6 : 0);
        mask = (uint8_t)(0x7fu << p->offset_y);
        other = (uint8_t)(0x7fu >> (8u - p->offset_y));
        return (uint8_t)(((back_byte(x, page) & mask) != 0u) ||
                         ((back_byte(x, page + 1) & other) != 0u));
    }
    if (p->dir_h == PACMAN_DIR_STOP) return 0u;
    if (p->dir_h == 0u) {
        mask = (uint8_t)(0x7fu << p->offset_y);
    } else {
        uint8_t extra = (uint8_t)(p->offset_y > 2u);
        page += extra;
        mask = extra ? (uint8_t)(0x7fu >> (8u - p->offset_y)) :
                       (uint8_t)(0x7fu << p->offset_y);
    }
    for (i = 0u; i <= 6u; i++)
        if (back_byte(x + i, page) & mask) return 1u;
    return 0u;
}

static void reset_actors(pacman_game_t *g) {
    uint8_t i;
    memset(g->actors, 0, sizeof(g->actors));
    g->actors[0].type = PACMAN_PLAYER;
    g->actors[0].x = 64u;
    g->actors[0].page_y = 3;
    g->actors[0].offset_y = 5u;
    g->actors[0].dir_h = g->actors[0].dir_v = PACMAN_DIR_STOP;
    for (i = 1u; i < PACMAN_ACTORS; i++) {
        pacman_actor_t *p = &g->actors[i];
        p->type = PACMAN_GHOST;
        p->x = (uint8_t)(i == 1u || i == 4u ? 76u : (i == 2u ? 75u : 77u));
        p->page_y = (int8_t)(i == 1u || i == 3u ? 4 : 5);
        p->dir_v = p->dir_h = 0u;
    }
    g->power_active = 0u;
    g->power_timer = 0u;
    g->frame = 0u;
}

static void init_dots(pacman_game_t *g) {
    memset(g->dots_mem, 0xff, sizeof(g->dots_mem));
    g->dots_left = PACMAN_DOTS;
}

void Pacman_CoreNewGame(pacman_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->random_state = seed ? seed : 0x6d2b79f5u;
    g->lives = 3u;
    g->level = 1u;
    g->level_speed = 200u;
    g->gobbing_end = 100u;
    init_dots(g);
    reset_actors(g);
    g->phase = PACMAN_READY;
}

void Pacman_CoreStart(pacman_game_t *g) {
    if (g->phase == PACMAN_GAME_OVER) {
        uint32_t seed = next_random(g);
        Pacman_CoreNewGame(g, seed);
    }
    if (g->phase != PACMAN_READY) return;
    /* StartGame() in the original releases ghosts inside their cage. */
    g->actors[1].page_y = 3;
    g->actors[2].page_y = 4;
    g->actors[3].page_y = 3;
    g->actors[4].page_y = 4;
    g->phase = PACMAN_PLAYING;
    g->sound_event = PACMAN_SOUND_START;
}

uint8_t Pacman_CoreDotVisible(const pacman_game_t *g, uint8_t number) {
    if (number >= PACMAN_DOTS) return 0u;
    return (uint8_t)((g->dots_mem[number / 8u] & (0x80u >> (number % 8u))) != 0u);
}

static void consume_dots(pacman_game_t *g) {
    uint8_t idx = 0u, page;
    const pacman_actor_t *p = &g->actors[0];
    for (page = 0u; page < 8u; page++) {
        uint8_t x;
        for (x = 0u; x < 128u; x++) {
            if (!pacman_dots[(uint16_t)page * 128u + x]) continue;
            if (idx < PACMAN_DOTS && Pacman_CoreDotVisible(g, idx) &&
                (int16_t)p->x < (int16_t)x && (int16_t)p->x > (int16_t)x - 6 &&
                ((p->page_y == (int8_t)page && p->offset_y < 6u) ||
                 ((p->page_y == (int8_t)page - 1 && p->offset_y > 5u)))) {
                g->dots_mem[idx / 8u] &= (uint8_t)~(0x80u >> (idx % 8u));
                if (g->dots_left) --g->dots_left;
                if (idx == 0u || idx == 1u || idx == 12u || idx == 13u ||
                    idx == 50u || idx == 51u || idx == 62u || idx == 63u) {
                    g->power_active = 1u;
                    g->power_timer = g->level_speed;
                    g->sound_event = PACMAN_SOUND_POWER;
                } else if (g->sound_event == PACMAN_SOUND_NONE) {
                    g->sound_event = PACMAN_SOUND_DOT;
                }
            }
            ++idx;
        }
    }
}

/* Original inclusive 6x7 collision box; eaten ghosts are harmless. */
static uint8_t touch_ghosts(pacman_game_t *g) {
    uint8_t i;
    const pacman_actor_t *p = &g->actors[0];
    int16_t px = p->x, py = (int16_t)p->page_y * 8 + p->offset_y;
    for (i = 1u; i < PACMAN_ACTORS; i++) {
        pacman_actor_t *q = &g->actors[i];
        int16_t qx = q->x, qy = (int16_t)q->page_y * 8 + q->offset_y;
        if (px + 6 < qx || px > qx + 6 || py + 6 < qy || py > qy + 6) continue;
        if (g->power_active) {
            if (!q->guber) g->sound_event = PACMAN_SOUND_GHOST;
            q->guber = 1u;
        } else if (!q->guber) return 1u;
    }
    return 0u;
}

static void move_actors(pacman_game_t *g) {
    uint8_t i;
    for (i = 0u; i < PACMAN_ACTORS; i++) {
        pacman_actor_t *p = &g->actors[i];
        uint8_t old_x = p->x, old_shift = p->offset_y;
        int8_t old_y = p->page_y;
        uint8_t can_move = (uint8_t)((g->frame % 2u == 0u) || (i == 0u) ||
                                      (g->level_speed <= 160u));
        if (can_move && p->page_y >= 0 && p->page_y < 8) {
            if (p->dir_v == 1u) ++p->x;
            if (p->dir_v == 0u) {
                if (i != 0u || p->page_y != 3 || p->x != 86u) --p->x;
            }
        }
        if (Pacman_CoreCollisionBack(p, 1u)) {
            p->dir_v = i ? (uint8_t)(next_random(g) % 2u) : PACMAN_DIR_STOP;
            p->x = old_x;
        }
        if (can_move) {
            if (p->dir_h == 1u) {
                if (p->offset_y < 7u) ++p->offset_y;
                else {p->offset_y = 0u; ++p->page_y; if (p->page_y == 9) p->page_y = -1;}
            } else if (p->dir_h == 0u) {
                if (p->offset_y) --p->offset_y;
                else {p->offset_y = 7u; --p->page_y; if (p->page_y == -2) p->page_y = 8;}
            }
        }
        if (Pacman_CoreCollisionBack(p, 0u)) {
            p->dir_h = i ? (uint8_t)(next_random(g) % 2u) : PACMAN_DIR_STOP;
            p->page_y = old_y;
            p->offset_y = old_shift;
        }
        if (!i) {
            if (g->frame % 2u == 0u) {
                if (p->dir_h == 1u) p->direction_anim = 0u;
                if (p->dir_h == 0u) p->direction_anim = 6u;
                if (p->dir_v == 1u) p->direction_anim = 9u;
                if (p->dir_v == 0u) p->direction_anim = 3u;
                if (!p->switch_anim) {
                    if (p->anim < 2u) ++p->anim;
                    else p->switch_anim = 1u;
                } else if (p->anim) --p->anim;
                else p->switch_anim = 0u;
            }
        } else {
            if (g->frame == 0u || g->frame == 12u) {
                p->direction_anim = (uint8_t)(p->dir_h == 0u ? 2u : 0u);
            }
            if (p->guber && p->x >= 74u && p->x <= 76u &&
                p->page_y >= 2 && p->page_y <= 4) p->guber = 0u;
            if ((g->frame % 2u) == 0u) p->anim = (uint8_t)!p->anim;
        }
    }
}

static void next_level(pacman_game_t *g) {
    if (g->level < 255u) ++g->level;
    if (g->level_speed > 10u) {
        g->level_speed = (uint8_t)(g->level_speed - 10u);
        if (g->level_speed == 160u || g->level_speed == 120u ||
            g->level_speed == 80u || g->level_speed == 40u ||
            g->level_speed == 10u) {
            if (g->lives < 3u) ++g->lives;
        }
    }
    g->gobbing_end = (uint8_t)(g->level_speed / 2u);
    init_dots(g);
    reset_actors(g);
    /* Original INGAME remains set after NEWLEVEL: next round starts automatically. */
    g->phase = PACMAN_PLAYING;
}

void Pacman_CoreTick(pacman_game_t *g, pacman_direction_t direction) {
    pacman_actor_t *p = &g->actors[0];
    if (g->phase == PACMAN_DYING || g->phase == PACMAN_CLEARED) {
        if (g->delay_ticks) --g->delay_ticks;
        if (!g->delay_ticks) {
            if (g->phase == PACMAN_DYING) {
                reset_actors(g);
                g->phase = PACMAN_PLAYING;
            } else next_level(g);
        }
        return;
    }
    if (g->phase != PACMAN_PLAYING) return;
    if (direction == PACMAN_UP) p->dir_h = 0u;
    if (direction == PACMAN_DOWN) p->dir_h = 1u;
    if (direction == PACMAN_LEFT) p->dir_v = 0u;
    if (direction == PACMAN_RIGHT) p->dir_v = 1u;
    if (g->power_timer) {
        if (--g->power_timer == 0u) g->power_active = 0u;
    }
    g->frame = (uint8_t)(g->frame < 24u ? g->frame + 1u : 0u);
    if (touch_ghosts(g)) goto hit;
    move_actors(g);
    consume_dots(g);
    if (touch_ghosts(g)) goto hit;
    if (g->dots_left == 0u) {
        g->phase = PACMAN_CLEARED;
        g->delay_ticks = 44u; /* ~1 s victory interlude, no delay(). */
        g->sound_event = PACMAN_SOUND_LEVEL;
    }
    return;
hit:
    g->sound_event = PACMAN_SOUND_DEATH;
    if (g->lives > 1u) {
        --g->lives;
        g->phase = PACMAN_DYING;
        g->delay_ticks = 18u; /* ~400 ms without blocking other services. */
    } else {
        g->lives = 0u;
        g->phase = PACMAN_GAME_OVER;
    }
}

pacman_sound_t Pacman_CoreTakeSound(pacman_game_t *g) {
    pacman_sound_t event = (pacman_sound_t)g->sound_event;
    g->sound_event = PACMAN_SOUND_NONE;
    return event;
}
