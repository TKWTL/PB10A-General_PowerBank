/* Tiny Invaders v3.1 (Daniel C, GPL-3.0-or-later).
 * Nonblocking rewrite of original SPACE, movement, shield and shoot logic.
 * Original source uses Tiny_Flip as a simulation tick; this port separates
 * deterministic tick processing from read-only SSD1306 page rendering. */
#include "invaders_core.h"
#include "invaders_assets.h"
#include "rle.h"
#include <string.h>

/* Cached 128x64 scrolling background; game owns its single shared buffer. */
static const uint8_t *s_cached_background;
void Invaders_CoreBindBackground(const uint8_t *background) {
    s_cached_background = background;
}
#define INVADERS_SHIP_MIN 5u
#define INVADERS_SHIP_MAX 114u
#define INVADERS_SHOT_NONE (-1)
#define INVADERS_ENEMY_SHOT_NONE 16u
#define INVADERS_UFO_NONE (-120)
#define INVADERS_MAX_LIVES 3u
#define INVADERS_INITIAL_DELAY 30u
#define INVADERS_LAST_LEVEL 9u

static uint32_t next_random(invaders_game_t *g) {
    uint32_t v = g->random_state;
    if (!v) v = 0x6d2b79f5u;
    v ^= v << 13;
    v ^= v >> 17;
    v ^= v << 5;
    g->random_state = v;
    return v;
}

static uint8_t alive(int8_t a) {
    return (uint8_t)(a >= 0 && a <= 5);
}

static void refresh_count(invaders_game_t *g) {
    uint8_t row, col, n = 0u;
    for (row = 0u; row < 4u; ++row)
        for (col = 0u; col < 6u; ++col)
            if (alive(g->monsters[row][col])) ++n;
    g->monster_count = n;
    g->formation_frame_max = (uint8_t)(n / 8u);
}

static void refresh_floor(invaders_game_t *g) {
    int8_t row;
    for (row = 3; row >= 0; --row) {
        uint8_t col;
        for (col = 0u; col < 6u; ++col) {
            if (g->monsters[(uint8_t)row][col] != -1) {
                g->formation_floor = (uint8_t)row;
                return;
            }
        }
    }
    g->formation_floor = 0u;
}

static uint8_t reached_floor(const invaders_game_t *g) {
    return (uint8_t)(g->formation_y + g->formation_floor + 1u >= 7u);
}

static void load_level(invaders_game_t *g) {
    uint8_t row, col;
    for (row = 0u; row < 4u; ++row)
        for (col = 0u; col < 6u; ++col)
            g->monsters[row][col] = invaders_levels[(uint16_t)g->level * 24u + (uint16_t)row * 6u + col];
    memset(g->shields, 0xff, sizeof(g->shields));
    g->shield_removed = 0u;
    g->shot_y = INVADERS_SHOT_NONE;
    g->shot_x = 0u;
    g->shot_half = 0u;
    g->shot_ready = 2u;
    g->enemy_shot_x = 16u;
    g->enemy_shot_half_y = INVADERS_ENEMY_SHOT_NONE;
    g->ufo_x = INVADERS_UFO_NONE;
    g->ship_dead = 0u;
    g->ship_target = 54u;
    g->ship_pos = 56u;
    g->ship_bg_offset = (uint8_t)(g->ship_pos / 14u + 52u);
    g->formation_x = 20u;
    g->formation_y = (uint8_t)(g->level > 3u ? 1u : 0u);
    g->formation_y_shift = 0u;
    g->formation_right = 1u;
    g->formation_frame = 0u;
    g->formation_frame_max = 8u;
    g->formation_floor = 3u;
    g->enemy_fire_cadence = 0u;
    g->anim = 0u;
    g->one_frame = 0u;
    g->death_ticks = 0u;
    refresh_count(g);
}

void Invaders_CoreInit(invaders_game_t *g, uint32_t seed) {
    memset(g, 0, sizeof(*g));
    g->random_state = seed ? seed : 0x6d2b79f5u;
    g->lives = INVADERS_MAX_LIVES;
    g->phase = INVADERS_TITLE;
    load_level(g);
}

void Invaders_CoreStart(invaders_game_t *g) {
    if (g->phase != INVADERS_TITLE && g->phase != INVADERS_GAME_OVER) return;
    if (g->phase == INVADERS_GAME_OVER) {
        uint32_t seed = next_random(g);
        Invaders_CoreInit(g, seed);
    }
    load_level(g);
    g->delay_ticks = INVADERS_INITIAL_DELAY;
    g->phase = INVADERS_READY;
    g->sound_event = INVADERS_SOUND_START;
}

/* Six 8-pixel segments form three two-piece shields at y page 6.
 * Both player and enemy bullets mutate exactly one bit when they touch. */
static int8_t shield_piece(int16_t x, uint8_t *bit) {
    static const uint8_t starts[6] = {19u,27u,54u,62u,89u,97u};
    uint8_t i;
    for (i = 0u; i < 6u; ++i) {
        if (x >= starts[i] && x < (int16_t)starts[i] + 8) {
            *bit = (uint8_t)(x - starts[i]);
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t shield_hit(invaders_game_t *g, int16_t x, uint8_t page) {
    uint8_t bit;
    int8_t segment;
    uint8_t mask;
    if (g->shield_removed || page != 6u) return 0u;
    segment = shield_piece(x, &bit);
    if (segment < 0) return 0u;
    mask = (uint8_t)(0x80u >> bit);
    if (!(g->shields[(uint8_t)segment] & mask)) return 0u;
    g->shields[(uint8_t)segment] &= (uint8_t)~mask;
    return 1u;
}

static void enemy_fire(invaders_game_t *g) {
    uint8_t row, col;
    if (g->enemy_shot_half_y != INVADERS_ENEMY_SHOT_NONE) return;
    row = (uint8_t)(next_random(g) % 3u);
    col = (uint8_t)(next_random(g) % 6u);
    if (g->monsters[row][col] == -1) return;
    g->enemy_shot_x = (uint8_t)(g->formation_x + 7u + col * 14u);
    g->enemy_shot_half_y = (uint8_t)((g->formation_y + row) * 2u + 1u);
}

static void update_enemy_shot(invaders_game_t *g) {
    if (g->enemy_shot_half_y == INVADERS_ENEMY_SHOT_NONE) return;
    if (g->enemy_shot_half_y >= 14u && g->enemy_shot_half_y <= 15u &&
        g->enemy_shot_x >= g->ship_pos &&
        g->enemy_shot_x <= (uint16_t)g->ship_pos + 14u)
        g->ship_dead = 1u;
    if (shield_hit(g, g->enemy_shot_x, (uint8_t)(g->enemy_shot_half_y / 2u)))
        g->enemy_shot_half_y = INVADERS_ENEMY_SHOT_NONE;
    else
        ++g->enemy_shot_half_y;
}

static void destroy_enemy(invaders_game_t *g, uint8_t row, uint8_t col) {
    if (!alive(g->monsters[row][col])) return;
    g->monsters[row][col] = 8;
    g->shot_y = INVADERS_SHOT_NONE;
    g->sound_event = INVADERS_SOUND_HIT;
    refresh_count(g);
}

static void update_player_shot(invaders_game_t *g) {
    int16_t shot_y, delta_x, delta_y;
    uint8_t row, col;
    if (g->shot_y < 0) {
        if (g->shot_ready < 2u) ++g->shot_ready;
        return;
    }
    g->shot_half ^= 1u;
    if (g->shot_half) --g->shot_y;
    if (g->shot_y < 0) return;
    shot_y = (int16_t)g->shot_y * 8 + (int16_t)(g->shot_half + 1u) * 4;
    delta_x = (int16_t)g->shot_x - g->formation_x;
    delta_y = shot_y - (int16_t)g->formation_y * 8;
    if (delta_x >= 0 && delta_x <= 84 && delta_y >= 0 && delta_y <= 32) {
        col = (uint8_t)(delta_x / 14);
        row = (uint8_t)(delta_y / 8);
        if (col < 6u && row < 4u) destroy_enemy(g, row, col);
    }
    if (g->shot_y == 0 && g->ufo_x != INVADERS_UFO_NONE &&
        g->shot_x >= g->ufo_x && (int16_t)g->shot_x <= g->ufo_x + 14) {
        if (g->lives < INVADERS_MAX_LIVES) ++g->lives;
        g->ufo_x = INVADERS_UFO_NONE;
        g->sound_event = INVADERS_SOUND_UFO;
        g->shot_y = INVADERS_SHOT_NONE;
    }
    if (g->shot_y >= 0 && shield_hit(g, g->shot_x, (uint8_t)g->shot_y))
        g->shot_y = INVADERS_SHOT_NONE;
}

static void update_monster_explosions(invaders_game_t *g) {
    uint8_t row, col;
    for (row = 0u; row < 4u; ++row)
        for (col = 0u; col < 6u; ++col) {
            int8_t *v = &g->monsters[row][col];
            if (*v >= 11) *v = -1;
            else if (*v >= 8) ++*v;
        }
}

static void move_formation(invaders_game_t *g) {
    if (g->formation_right) {
        if (g->formation_x < 44u) {g->formation_x = (uint8_t)(g->formation_x + 2u);return;}
        g->formation_right = 0u;
    } else {
        if (g->formation_x > 0u) {g->formation_x = (uint8_t)(g->formation_x - 2u);return;}
        g->formation_right = 1u;
    }
    if (g->formation_y_shift < 7u) {
        uint8_t v = (uint8_t)(g->formation_y_shift + 4u);
        g->formation_y_shift = v > 7u ? 7u : v;
    } else {
        if (g->formation_y < 7u) ++g->formation_y;
        g->formation_y_shift = 0u;
    }
}

static void update_ship(invaders_game_t *g, invaders_move_t move) {
    if (move == INVADERS_MOVE_LEFT) {
        g->ship_target = g->ship_target > INVADERS_SHIP_MIN + 5u ?
            (uint8_t)(g->ship_target - 6u) : INVADERS_SHIP_MIN;
    } else if (move == INVADERS_MOVE_RIGHT) {
        g->ship_target = g->ship_target < INVADERS_SHIP_MAX - 5u ?
            (uint8_t)(g->ship_target + 6u) : INVADERS_SHIP_MAX;
    }
    if (g->ship_target > (uint16_t)g->ship_pos + 2u) {
        g->ship_pos = (uint8_t)(g->ship_pos + (g->ship_target - g->ship_pos) / 3u);
    } else if ((uint16_t)g->ship_target + 2u < g->ship_pos) {
        g->ship_pos = (uint8_t)(g->ship_pos - (g->ship_pos - g->ship_target) / 3u);
    }
    if (g->ship_pos < INVADERS_SHIP_MIN) g->ship_pos = INVADERS_SHIP_MIN;
    if (g->ship_pos > INVADERS_SHIP_MAX) g->ship_pos = INVADERS_SHIP_MAX;
    g->ship_bg_offset = (uint8_t)(g->ship_pos / 14u + 52u);
}

static void die(invaders_game_t *g) {
    g->ship_dead = 1u;
    g->phase = INVADERS_DYING;
    g->death_ticks = 30u;
    g->sound_event = INVADERS_SOUND_DIE;
}

void Invaders_CoreTick(invaders_game_t *g, invaders_move_t move, uint8_t shoot) {
    if (g->phase == INVADERS_READY || g->phase == INVADERS_LEVEL_CLEAR) {
        if (g->delay_ticks) --g->delay_ticks;
        if (!g->delay_ticks) {
            if (g->phase == INVADERS_LEVEL_CLEAR) {
                if (g->level < INVADERS_LAST_LEVEL) ++g->level;
                load_level(g);
                g->phase = INVADERS_READY;
                g->delay_ticks = INVADERS_INITIAL_DELAY;
            } else g->phase = INVADERS_PLAYING;
        }
        return;
    }
    if (g->phase == INVADERS_DYING) {
        g->one_frame ^= 1u;
        if (g->death_ticks) --g->death_ticks;
        if (!g->death_ticks) {
            if (reached_floor(g) || g->lives <= 1u) {
                g->lives = 0u;
                g->phase = INVADERS_GAME_OVER;
            } else {
                --g->lives;
                g->ship_dead = 0u;
                g->shot_y = INVADERS_SHOT_NONE;
                g->enemy_shot_half_y = INVADERS_ENEMY_SHOT_NONE;
                g->phase = INVADERS_READY;
                g->delay_ticks = INVADERS_INITIAL_DELAY;
            }
        }
        return;
    }
    if (g->phase != INVADERS_PLAYING) return;
    if (g->monster_count == 0u) {
        g->phase = INVADERS_LEVEL_CLEAR;
        g->delay_ticks = INVADERS_INITIAL_DELAY;
        g->sound_event = INVADERS_SOUND_LEVEL;
        return;
    }
    g->one_frame ^= 1u;
    update_monster_explosions(g);
    update_enemy_shot(g);
    if (g->ufo_x != INVADERS_UFO_NONE) {
        g->ufo_x -= 2;
        if (g->ufo_x <= -20) g->ufo_x = INVADERS_UFO_NONE;
    }
    if (g->formation_x >= 26u && g->formation_x <= 28u &&
        g->formation_y == 2u && g->formation_y_shift == 4u)
        g->ufo_x = 127;
    update_ship(g, move);
    if (g->formation_frame < g->formation_frame_max) ++g->formation_frame;
    else {
        refresh_floor(g);
        g->anim ^= 1u;
        move_formation(g);
        g->formation_frame = 0u;
    }
    if (shoot && g->shot_ready >= 2u && g->shot_y < 0) {
        g->shot_ready = 0u;
        g->shot_y = 6;
        g->shot_x = (uint8_t)(g->ship_pos + 6u);
        g->shot_half = 0u;
        g->sound_event = INVADERS_SOUND_SHOT;
    }
    update_player_shot(g);
    if (g->enemy_fire_cadence <= (uint8_t)(9u - g->level))
        ++g->enemy_fire_cadence;
    else {
        g->enemy_fire_cadence = 0u;
        enemy_fire(g);
    }
    if (!g->shield_removed && g->formation_y >=
        (uint8_t)(2u + (4u - (g->formation_floor + 1u)))) {
        memset(g->shields, 0, sizeof(g->shields));
        g->shield_removed = 1u;
    }
    if (g->ship_dead || reached_floor(g)) die(g);
}

/* Original inline pixel writer, now pure: no bullet movement/impact mutations. */
static uint8_t shield_shape(uint8_t half, uint8_t pos) {
    if (pos == 0u) return half ? 0x0fu : 0xf0u;
    if (pos == 1u) return half ? 0x0fu : 0xfcu;
    if (pos >= 2u && pos <= 5u) return 0x0fu;
    if (pos == 6u) return half ? 0xfcu : 0x0fu;
    return half ? 0xf0u : 0x0fu;
}

static uint8_t shield_pixel(const invaders_game_t *g, uint8_t x, uint8_t page) {
    uint8_t bit, mask;
    int8_t segment;
    if (page != 6u || g->shield_removed) return 0u;
    segment = shield_piece(x, &bit);
    if (segment < 0) return 0u;
    mask = (uint8_t)(0x80u >> bit);
    if (!(g->shields[(uint8_t)segment] & mask)) return 0u;
    return shield_shape((uint8_t)segment & 1u, bit);
}

static uint8_t monster_pixel(const invaders_game_t *g, uint8_t x, uint8_t page) {
    uint8_t row, col, combined = 0u;
    for (row = 0u; row < 4u; ++row) {
        int16_t yy = (int16_t)(g->formation_y + row) * 8 + g->formation_y_shift;
        for (col = 0u; col < 6u; ++col) {
            int8_t type = g->monsters[row][col];
            int16_t xx = (int16_t)g->formation_x + col * 14;
            uint16_t index;
            uint8_t glyph;
            if (type < 0 || type > 11 || x < xx || (int16_t)x >= xx + 14) continue;
            index = (uint16_t)((type < 8 ? type + g->anim : type) * 14 + (x - xx));
            if (index >= sizeof(invaders_monsters)) continue;
            glyph = invaders_monsters[index];
            if (page == (uint8_t)(yy / 8)) combined |= (uint8_t)(glyph << (yy % 8));
            if (yy % 8 && page == (uint8_t)(yy / 8 + 1))
                combined |= (uint8_t)(glyph >> (8 - (yy % 8)));
        }
    }
    return combined;
}

uint8_t Invaders_CorePixel(const invaders_game_t *g, uint8_t x, uint8_t page) {
    uint8_t b, bg;
    /* TITLE/GAME_OVER are decoded as one whole image by CoreRender().
     * CorePixel is only used for the active-scene layer composition. */
    if (g->phase == INVADERS_TITLE || g->phase == INVADERS_GAME_OVER)
        return 0u;
    bg = (uint8_t)(((uint16_t)g->ship_bg_offset + x) & 127u);
    b = (uint8_t)~(s_cached_background ?
                  s_cached_background[(uint16_t)page * 128u + bg] : 0u);
    if (page == 7u) {
        if (x < (uint8_t)(g->lives * 5u)) b |= invaders_lives[x];
        if (x >= g->ship_pos && x < (uint16_t)g->ship_pos + 13u) {
            uint8_t k = (uint8_t)(x - g->ship_pos +
                (g->ship_dead ? 12u * g->one_frame : 0u));
            if (k < sizeof(invaders_ship)) b |= invaders_ship[k];
        }
    }
    if (page == 0u && g->ufo_x != INVADERS_UFO_NONE &&
        x >= g->ufo_x && (int16_t)x < g->ufo_x + 14) {
        uint8_t k = (uint8_t)(x - g->ufo_x);
        b |= invaders_monsters[(uint16_t)(6u + g->one_frame) * 14u + k];
    }
    b |= monster_pixel(g, x, page);
    b |= shield_pixel(g, x, page);
    if (g->shot_y >= 0 && page == (uint8_t)g->shot_y && x == g->shot_x)
        b |= invaders_shoot[g->shot_half];
    if (g->enemy_shot_half_y != INVADERS_ENEMY_SHOT_NONE &&
        page == (uint8_t)(g->enemy_shot_half_y / 2u) && x == g->enemy_shot_x)
        b |= (g->enemy_shot_half_y & 1u) ? 0xf0u : 0x0fu;
    return b;
}

void Invaders_CoreRender(const invaders_game_t *g, uint8_t *framebuffer) {
    uint8_t page, x;
    if (!g || !framebuffer) return;
    if (g->phase == INVADERS_TITLE || g->phase == INVADERS_GAME_OVER) {
        /* Render original 1024-byte title frame without a second buffer. */
        (void)RLE_Decode(invaders_intro_rle,sizeof(invaders_intro_rle),
                         framebuffer,1024u,RLE_WRITE);
        return;
    }
    for (page = 0u; page < 8u; ++page)
        for (x = 0u; x < 128u; ++x)
            framebuffer[(uint16_t)page * 128u + x] = Invaders_CorePixel(g, x, page);
}

invaders_sound_t Invaders_CoreTakeSound(invaders_game_t *g) {
    invaders_sound_t event = (invaders_sound_t)g->sound_event;
    g->sound_event = INVADERS_SOUND_NONE;
    return event;
}
