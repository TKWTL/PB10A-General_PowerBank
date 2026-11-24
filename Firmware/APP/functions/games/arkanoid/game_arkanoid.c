/*
 * Tiny Arkanoid by Daniel Champagne (2018–2021), Tinyjoypad release 2025.
 * https://sites.google.com/view/arduino-collection
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Adapted from the supplied ATtiny85 sketch. The platform layer supplies
 * fixed-step scheduling, debounced keys, sound, and the OLED framebuffer.
 */
#include "game_arkanoid.h"
#include "arkanoid_assets.h"
#include "beep_service.h"
#include "buttons.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "millis.h"
#include <stdint.h>
#include <string.h>

#define LOGIC_MS 2u
#define FRAME_MS 32u
#define WAKE_GAP_MS 150u
#define PADDLE_TICKS 8u
#define NEXT_LEVEL_TICKS (400u / LOGIC_MS)
#define PIXEL 100
#define LEVEL_SIZE 30u
#define LEVEL_COUNT (sizeof(level_data) / LEVEL_SIZE)
#define SCREEN_WIDTH 128u
#define SCREEN_PAGES 8u

typedef enum {
    ARKANOID_TITLE,
    ARKANOID_SERVE,
    ARKANOID_PLAYING,
    ARKANOID_LEVEL_CLEAR
} arkanoid_phase_t;

typedef struct {
    int16_t x, y, dx, dy; /* Hundredths of a pixel. */
} ball_t;

typedef struct {
    ball_t ball;
    uint8_t blocks[6][5];
    uint16_t level, clear_ticks;
    uint8_t lives, paddle_y, paddle_ticks, ball_ticks, ball_period, reflect_ticks, reflect_frame;
    arkanoid_phase_t phase;
} arkanoid_game_t;

static arkanoid_game_t s_game;
static uint32_t s_last, s_accum;
static uint8_t s_active;

/* Approximate the original AVR delay-loop tones on the shared PWM. */
#define TONE(hint, count) (500000u / (255u - (hint))), (((count) * (255u - (hint)) + 250u) / 500u)
static const uint16_t s_start_sound[] = {TONE(125u, 155u), TONE(125u, 155u), BEEP_END};
static const uint16_t s_paddle_sound[] = {TONE(60u, 10u), BEEP_END};
static const uint16_t s_block_sound[] = {TONE(150u, 10u), BEEP_END};
static const uint16_t s_reflect_sound[] = {TONE(210u, 50u), BEEP_END};
static const uint16_t s_miss_sound[] = {TONE(200u, 100u), TONE(150u, 100u), TONE(100u, 100u), TONE(50u, 100u), BEEP_END};
static const uint16_t s_next_sound[] = {TONE(60u, 100u), TONE(80u, 100u), TONE(100u, 100u), TONE(120u, 100u), TONE(140u, 100u), BEEP_END};
#undef TONE

static void play(const uint16_t *sound) {
    if (!beep_busy())
        (void)beep_enqueue(sound);
}

static void reset_ball(arkanoid_game_t *game, int16_t initial_dy) {
    game->phase = ARKANOID_SERVE;
    game->paddle_y = 20u;
    game->paddle_ticks = 0u;
    game->ball_ticks = 0u;
    game->reflect_ticks = 0u;
    game->reflect_frame = 0u;
    game->ball = (ball_t){8 * PIXEL, 30 * PIXEL, PIXEL, initial_dy};
}

static void load_level(arkanoid_game_t *game, int16_t initial_dy) {
    unsigned offset = ((game->level - 1u) % LEVEL_COUNT) * LEVEL_SIZE;
    memcpy(game->blocks, level_data + offset, sizeof(game->blocks));
    reset_ball(game, initial_dy);
}

static void start_game(arkanoid_game_t *game) {
    game->level = 1u;
    game->lives = 3u;
    game->ball_period = 16u;
    load_level(game, 47);
    play(s_start_sound);
}

static uint8_t check_level_ended(const arkanoid_game_t *game) {
    for (unsigned row = 0; row < 6u; ++row)
        for (unsigned col = 0; col < 5u; ++col)
            if (game->blocks[row][col] != UINT8_MAX && game->blocks[row][col] != 5u)
                return 0u;
    return 1u;
}

/* Original RecupePositionOnGrid: columns are six pixels wide and the
 * six rows use the original staggered y boundaries. */
static uint8_t hit_block(arkanoid_game_t *game, const ball_t *next) {
    if (next->x < 66 * PIXEL || next->x >= 96 * PIXEL ||
        next->y < 8 * PIXEL || next->y >= 55 * PIXEL)
        return 0u;

    unsigned col = (unsigned)(next->x - 66 * PIXEL) / (6u * PIXEL);
    unsigned row = 0u;
    static const int16_t ends[] = {16 * PIXEL, 23 * PIXEL, 31 * PIXEL, 40 * PIXEL, 48 * PIXEL};
    while (row < sizeof(ends) / sizeof(ends[0]) && next->y >= ends[row])
        ++row;
    uint8_t *block = &game->blocks[row][col];
    if (*block == UINT8_MAX)
        return 0u;
    if (*block == 5u) {
        play(s_reflect_sound);
        game->reflect_frame = 0u;
        game->reflect_ticks = 0u;
    } else {
        play(s_block_sound);
        *block = UINT8_MAX;
    }
    return 1u;
}

/* Original SimulMove searches alternate vectors in this order. */
static ball_t simulate_move(ball_t next, unsigned attempt) {
    switch (attempt) {
    case 1:
        next.dx = -next.dx;
        break;
    case 2:
        next.dy = -next.dy;
        break;
    case 3:
        next.dx = -next.dx;
        next.dy = -next.dy;
        break;
    case 4: {
        int16_t vx = next.dx;
        next.dx = -next.dy;
        next.dy = -vx;
        break;
    }
    case 5:
    case 6:
        next.x += PIXEL;
        next.dx = -PIXEL;
        next.dy = attempt == 5u ? PIXEL : -PIXEL;
        break;
    default:
        break;
    }
    next.x += next.dx;
    next.y += next.dy;
    return next;
}

static void update_ball(arkanoid_game_t *game) {
    ball_t next = game->ball;
    int16_t paddle_angle = 0;
    for (unsigned attempt = 0u; attempt <= 6u; ++attempt) {
        next = simulate_move(game->ball, attempt);
        if (next.x > 106 * PIXEL || next.y < 4 * PIXEL || next.y > 59 * PIXEL)
            continue;

        int16_t from_paddle = next.y - game->paddle_y * PIXEL;
        if (next.x >= 5 * PIXEL && next.x <= 6 * PIXEL && from_paddle >= 0 && from_paddle <= 16 * PIXEL) {
            paddle_angle = (from_paddle - 8 * PIXEL) / 8;
            play(s_paddle_sound);
            continue;
        }
        if (!hit_block(game, &next))
            break;
    }
    next.dy += paddle_angle;
    if (next.dy < -PIXEL)
        next.dy = -PIXEL;
    else if (next.dy > PIXEL)
        next.dy = PIXEL;
    game->ball = next;
}

static void advance(arkanoid_game_t *game, int direction) {
    if (game->phase == ARKANOID_TITLE)
        return;
    if (game->phase == ARKANOID_LEVEL_CLEAR) {
        if (--game->clear_ticks == 0u) {
            if (game->ball_period > 8u)
                game->ball_period -= 2u;
            game->level = game->level < UINT16_MAX ? game->level + 1u : 1u;
            load_level(game, 47);
        }
        return;
    }
    if (++game->paddle_ticks == PADDLE_TICKS) {
        game->paddle_ticks = 0u;
        int next = game->paddle_y + direction;
        if (next >= 4 && next <= 44)
            game->paddle_y = (uint8_t)next;
    }
    if (++game->reflect_ticks == 64u) {
        game->reflect_ticks = 0u;
        if (game->reflect_frame < 3u)
            ++game->reflect_frame;
    }
    if (game->phase == ARKANOID_SERVE) {
        game->ball.y = (game->paddle_y + 10u) * PIXEL;
        return;
    }
    if (++game->ball_ticks != game->ball_period)
        return;
    game->ball_ticks = 0u;
    update_ball(game);

    if (game->ball.x < 0) {
        play(s_miss_sound);
        if (game->lives) {
            --game->lives;
            reset_ball(game, 41);
        } else {
            game->phase = ARKANOID_TITLE;
        }
    } else if (check_level_ended(game)) {
        play(s_next_sound);
        game->phase = ARKANOID_LEVEL_CLEAR;
        game->clear_ticks = NEXT_LEVEL_TICKS;
    }
}

static uint8_t background(unsigned x, unsigned page, unsigned texture_column) {
    if (x > 105u)
        return back_right[page * 22u + x - 106u];
    if (page == 0u)
        return back_top[x];
    if (page == 7u)
        return back_bottom[x];
    return back_texture[texture_column];
}

static uint8_t block_byte(const arkanoid_game_t *game, unsigned x, unsigned page) {
    if (x < 67u || x >= 97u || page < 1u || page > 6u)
        return 0u;
    unsigned local_x = x - 67u;
    uint8_t block = game->blocks[page - 1u][local_x / 6u];
    if (block == UINT8_MAX)
        return 0u;
    unsigned column = local_x % 6u;
    uint8_t bytes = block_sprite[block * 6u + column];
    if (block == 5u)
        bytes |= reflect_sprite[game->reflect_frame * 6u + column];
    return bytes;
}

static uint8_t sprite_byte(uint32_t column, unsigned y, unsigned local_page) {
    return (uint8_t)((column << (y & 7u)) >> (local_page * 8u));
}

static uint8_t paddle_byte(const arkanoid_game_t *game, unsigned x, unsigned page) {
    unsigned base = game->paddle_y / 8u;
    if (x < 3u || x > 6u || page < base || page - base > 2u)
        return 0u;
    unsigned col = x - 3u;
    uint16_t column = (uint16_t)paddle_sprite[col] | ((uint16_t)paddle_sprite[col + 4u] << 8);
    return sprite_byte(column, game->paddle_y, page - base);
}

static uint8_t ball_byte(const arkanoid_game_t *game, int x, unsigned page) {
    /* The sprite extends one pixel to either side of the ball centre. */
    int column = x - ((game->ball.x - PIXEL) / PIXEL);
    unsigned y = (unsigned)(game->ball.y - PIXEL) / PIXEL;
    if (column < 0 || column >= (int)sizeof(ball_sprite) || page < y / 8u || page - y / 8u > 1u)
        return 0u;
    return sprite_byte(ball_sprite[column], y, page - y / 8u);
}

static uint8_t panel_byte(const arkanoid_game_t *game, unsigned x, unsigned page) {
    if (x >= 119u && x <= 121u && page >= 1u && page <= game->lives)
        return life_sprite[x - 119u];
    if (x < 117u || x > 123u || (page != 5u && page != 6u))
        return 0u;
    unsigned digit = (page == 5u ? game->level / 10u : game->level) % 10u;
    return digit_sprite[digit * 7u + x - 117u];
}

static void render(const arkanoid_game_t *game) {
    uint8_t *output = Disp_GetBufferPtr();
    if (!output)
        return;
    if (game->phase == ARKANOID_TITLE) {
        memcpy(output, title_image, sizeof(title_image));
    } else {
        for (unsigned page = 0u; page < SCREEN_PAGES; ++page) {
            unsigned texture_column = 0u;
            for (unsigned x = 0u; x < SCREEN_WIDTH; ++x) {
                if (++texture_column == sizeof(back_texture))
                    texture_column = 0u;
                uint8_t value = background(x, page, texture_column);
                if (game->phase != ARKANOID_LEVEL_CLEAR) {
                    value |= block_byte(game, x, page);
                    value |= ball_byte(game, (int)x, page);
                    value |= paddle_byte(game, x, page);
                    value |= panel_byte(game, x, page);
                }
                *output++ = value;
            }
        }
    }
    Disp_SendBuffer();
}

static int held(KeyIndex_t key) {
    KeyState_t state = KEY_GetState(key);
    return state == KeyState_ShortPress || state == KeyState_LongPress;
}

void Game_ArkanoidOnLeave(ui_t *ui) {
    (void)ui;
    s_active = 0u;
}

void Game_ArkanoidRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_BACK)
        return;

    uint32_t now = Millis();
    if (!s_active) {
        s_active = 1u;
        s_game.phase = ARKANOID_TITLE;
        s_last = now;
        s_accum = 0u;
    }

    if (ui->action == UI_ACTION_ENTER) {
        if (s_game.phase == ARKANOID_TITLE)
            start_game(&s_game);
        else if (s_game.phase == ARKANOID_SERVE)
            s_game.phase = ARKANOID_PLAYING;
    }
    ui->action = UI_ACTION_NONE;

    uint32_t elapsed = now - s_last;
    s_last = now;
    if (elapsed > WAKE_GAP_MS)
        s_accum = 0u;
    else if (!held(KeyIndex_CONF)) {
        /* The board turns 90° CCW as in Pac-Man. Native y becomes portrait x. */
        int direction = held(KeyIndex_DOWN) - held(KeyIndex_UP);
        s_accum += elapsed;
        while (s_accum >= LOGIC_MS) {
            advance(&s_game, direction);
            s_accum -= LOGIC_MS;
        }
    }

    render(&s_game);
    UI_RequestNextFrame(ui, FRAME_MS);
}
