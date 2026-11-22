/*
 * Breakout gameplay adapted from BreakoutClone:
 * https://github.com/mattiasjahnke/arduino-projects/tree/master/BreakoutClone
 * Copyright (c) 2016 Mattias Jähnke.
 * Original source licensed under MIT. The full MIT license is retained in
 * APP/functions/LICENSE-BREAKOUTCLONE.
 *
 * PB10A port: pure C, Q4 fixed-point motion, life/win/lose states, RAM-only
 * best score, five-key input, cooperative 60 Hz update and MiaoUI rendering.
 */
#include "game_breakout.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "fonts/fonts.h"
#include "buttons.h"
#include "millis.h"
#include <stdio.h>

#define BREAKOUT_COLS 8u
#define BREAKOUT_ROWS 5u
#define BREAKOUT_BLOCK_W 15
#define BREAKOUT_BLOCK_H 3
#define BREAKOUT_BLOCK_XSTEP 16
#define BREAKOUT_BLOCK_YSTEP 4
#define BREAKOUT_BLOCK_TOP 10
#define BREAKOUT_PADDLE_W 22
#define BREAKOUT_PADDLE_H 2
#define BREAKOUT_PADDLE_Y 62
#define BREAKOUT_BALL_R 2
#define BREAKOUT_Q 16
#define BREAKOUT_PADDLE_SPEED 40 /* 2.5 pixels at 60 Hz */
#define BREAKOUT_PADDLE_SLOW_SPEED 20 /* UP held: 1.25 px/tick */
#define BREAKOUT_PADDLE_FINE_SPEED 10 /* DOWN held: 0.625 px/tick */
#define BREAKOUT_LIVES 3u
#define BREAKOUT_BRICKS 32u

typedef enum {
    BREAKOUT_READY = 0,
    BREAKOUT_PLAYING,
    BREAKOUT_WON,
    BREAKOUT_OVER
} breakout_state_t;

typedef struct {
    uint8_t bricks[BREAKOUT_ROWS];
    breakout_state_t state;
    uint8_t active, paused, lives, left, frame_phase;
    uint16_t score;
    int16_t paddle_x, ball_x, ball_y, vx, vy; /* Q4 pixels */
    uint32_t last_ms, step_accum;             /* step_accum units: ms * 60 */
} breakout_game_t;

static breakout_game_t s_game;
static uint16_t s_best_score; /* RAM only, survives leaving/re-entering game. */
static uint8_t s_serve_seed;

static void breakout_attach_ball(void) {
    s_game.ball_x = (int16_t)(s_game.paddle_x + (BREAKOUT_PADDLE_W / 2) * BREAKOUT_Q);
    s_game.ball_y = (int16_t)((BREAKOUT_PADDLE_Y - BREAKOUT_BALL_R - 2) * BREAKOUT_Q);
}

static void breakout_reset(uint32_t now) {
    /* Eight columns, mirrored about the centre line (5 rows, 32 bricks). */
    static const uint8_t layout[BREAKOUT_ROWS] = {0xffu, 0x7eu, 0x3cu, 0x7eu, 0xffu};
    uint8_t i;
    for (i = 0u; i < BREAKOUT_ROWS; i++)
        s_game.bricks[i] = layout[i];
    s_game.left = BREAKOUT_BRICKS;
    s_game.lives = BREAKOUT_LIVES;
    s_game.score = 0u;
    s_game.paused = 0u;
    s_game.state = BREAKOUT_READY;
    s_game.paddle_x = (int16_t)(((UI_HOR_RES - BREAKOUT_PADDLE_W) / 2) * BREAKOUT_Q);
    s_game.vx = 0;
    s_game.vy = 0;
    s_game.last_ms = now;
    s_game.step_accum = 0u;
    breakout_attach_ball();
}

static uint8_t breakout_pressed(KeyIndex_t key) {
    KeyState_t state = KEY_GetState(key);
    return (uint8_t)(state == KeyState_ShortPress || state == KeyState_LongPress);
}

static void breakout_move_paddle(void) {
    uint8_t left = breakout_pressed(KeyIndex_LEFT);
    uint8_t right = breakout_pressed(KeyIndex_RIGHT);
    if (left != right) {
        int16_t speed = breakout_pressed(KeyIndex_DOWN) ?
                        BREAKOUT_PADDLE_FINE_SPEED :
                        (breakout_pressed(KeyIndex_UP) ?
                         BREAKOUT_PADDLE_SLOW_SPEED : BREAKOUT_PADDLE_SPEED);
        int16_t delta = left ? (int16_t)-speed : speed;
        s_game.paddle_x = (int16_t)(s_game.paddle_x + delta);
        if (s_game.paddle_x < 0)
            s_game.paddle_x = 0;
        if (s_game.paddle_x > (UI_HOR_RES - BREAKOUT_PADDLE_W) * BREAKOUT_Q)
            s_game.paddle_x = (UI_HOR_RES - BREAKOUT_PADDLE_W) * BREAKOUT_Q;
    }
    if (s_game.state == BREAKOUT_READY)
        breakout_attach_ball();
}

static void breakout_launch(void) {
    static const int16_t launch_vx[3] = {-14, 0, 14};
    s_serve_seed = (uint8_t)((s_serve_seed + 1u) % 3u);
    s_game.vx = launch_vx[s_serve_seed];
    s_game.vy = -20;
    s_game.paused = 0u;
    s_game.state = BREAKOUT_PLAYING;
}

/* Resolve one brick per tick. Removing it immediately prevents repeat hits.
 * Approach from the previous ball position determines which axis reverses. */
static void breakout_hit_brick(int16_t old_x, int16_t old_y) {
    uint8_t row, col;
    int16_t left, right, top, bottom;
    const int16_t radius = BREAKOUT_BALL_R * BREAKOUT_Q;
    for (row = 0u; row < BREAKOUT_ROWS; row++) {
        for (col = 0u; col < BREAKOUT_COLS; col++) {
            if (!(s_game.bricks[row] & (1u << col)))
                continue;
            left = (int16_t)((col * BREAKOUT_BLOCK_XSTEP) * BREAKOUT_Q);
            right = (int16_t)(left + BREAKOUT_BLOCK_W * BREAKOUT_Q);
            top = (int16_t)((BREAKOUT_BLOCK_TOP + row * BREAKOUT_BLOCK_YSTEP) * BREAKOUT_Q);
            bottom = (int16_t)(top + BREAKOUT_BLOCK_H * BREAKOUT_Q);
            if (s_game.ball_x + radius <= left ||
                s_game.ball_x - radius >= right ||
                s_game.ball_y + radius <= top ||
                s_game.ball_y - radius >= bottom)
                continue;

            s_game.bricks[row] &= (uint8_t)~(1u << col);
            --s_game.left;
            s_game.score += 10u;
            if (s_game.score > s_best_score)
                s_best_score = s_game.score;

            if (old_y + radius <= top && s_game.vy > 0) {
                s_game.ball_y = (int16_t)(top - radius);
                s_game.vy = (int16_t)-s_game.vy;
            } else if (old_y - radius >= bottom && s_game.vy < 0) {
                s_game.ball_y = (int16_t)(bottom + radius);
                s_game.vy = (int16_t)-s_game.vy;
            } else if (old_x + radius <= left && s_game.vx > 0) {
                s_game.ball_x = (int16_t)(left - radius);
                s_game.vx = (int16_t)-s_game.vx;
            } else if (old_x - radius >= right && s_game.vx < 0) {
                s_game.ball_x = (int16_t)(right + radius);
                s_game.vx = (int16_t)-s_game.vx;
            } else {
                s_game.vy = (int16_t)-s_game.vy;
            }
            if (s_game.left == 0u) {
                s_game.state = BREAKOUT_WON;
                s_game.paused = 0u;
            }
            return;
        }
    }
}

static void breakout_step_ball(void) {
    int16_t old_x = s_game.ball_x, old_y = s_game.ball_y;
    const int16_t radius = BREAKOUT_BALL_R * BREAKOUT_Q;
    const int16_t paddle_top = BREAKOUT_PADDLE_Y * BREAKOUT_Q;
    int16_t hit_offset;
    s_game.ball_x = (int16_t)(s_game.ball_x + s_game.vx);
    s_game.ball_y = (int16_t)(s_game.ball_y + s_game.vy);

    if (s_game.ball_x < radius) {
        s_game.ball_x = radius;
        if (s_game.vx < 0) s_game.vx = (int16_t)-s_game.vx;
    } else if (s_game.ball_x > (UI_HOR_RES - BREAKOUT_BALL_R - 1) * BREAKOUT_Q) {
        s_game.ball_x = (UI_HOR_RES - BREAKOUT_BALL_R - 1) * BREAKOUT_Q;
        if (s_game.vx > 0) s_game.vx = (int16_t)-s_game.vx;
    }
    if (s_game.ball_y < (8 + BREAKOUT_BALL_R) * BREAKOUT_Q) {
        s_game.ball_y = (8 + BREAKOUT_BALL_R) * BREAKOUT_Q;
        if (s_game.vy < 0) s_game.vy = (int16_t)-s_game.vy;
    }

    /* The ball can rebound from the paddle only when crossing it downward. */
    if (s_game.vy > 0 && old_y + radius <= paddle_top &&
        s_game.ball_y + radius >= paddle_top &&
        s_game.ball_x + radius >= s_game.paddle_x &&
        s_game.ball_x - radius <= s_game.paddle_x + BREAKOUT_PADDLE_W * BREAKOUT_Q) {
        s_game.ball_y = (int16_t)(paddle_top - radius);
        hit_offset = (int16_t)(s_game.ball_x - s_game.paddle_x -
                              (BREAKOUT_PADDLE_W / 2) * BREAKOUT_Q);
        /* Seven paddle zones produce seven discrete outgoing directions. */
        if (hit_offset < -8 * BREAKOUT_Q) s_game.vx = -21;
        else if (hit_offset < -5 * BREAKOUT_Q) s_game.vx = -14;
        else if (hit_offset < -2 * BREAKOUT_Q) s_game.vx = -7;
        else if (hit_offset <= 2 * BREAKOUT_Q) s_game.vx = 0;
        else if (hit_offset <= 5 * BREAKOUT_Q) s_game.vx = 7;
        else if (hit_offset <= 8 * BREAKOUT_Q) s_game.vx = 14;
        else s_game.vx = 21;
        s_game.vy = -20;
    }

    if (s_game.ball_y - radius > UI_VER_RES * BREAKOUT_Q) {
        if (--s_game.lives == 0u) {
            s_game.state = BREAKOUT_OVER;
        } else {
            s_game.state = BREAKOUT_READY;
            breakout_attach_ball();
        }
        return;
    }
    breakout_hit_brick(old_x, old_y);
}

static void breakout_tick(void) {
    if (s_game.state == BREAKOUT_PLAYING && s_game.paused)
        return;
    breakout_move_paddle();
    if (s_game.state == BREAKOUT_PLAYING)
        breakout_step_ball();
}

static void breakout_draw(void) {
    uint8_t color = 1u, row, col;
    char stats[32];
    const char *state_text = 0, *action_text = 0;
    int16_t x, y;
    uint16_t width;
    Disp_SetMaxClipWindow();
    Disp_ClearBuffer();
    Disp_SetDrawColor(&color);
    Disp_SetFont(font_home_h6w4);
    (void)snprintf(stats, sizeof(stats), "SCORE:%u HI:%u",
                   (unsigned)s_game.score, (unsigned)s_best_score);
    Disp_DrawStr(1u, 7u, stats);
    /* Remaining lives are drawn as small balls at the upper-right. */
    for (col = 0u; col < s_game.lives; col++)
        Disp_DrawDisc((uint16_t)(124u - 10u * col), 4u, 2u, U8G2_DRAW_ALL);
    Disp_DrawLine(0u, 8u, UI_HOR_RES - 1u, 8u);
    for (row = 0u; row < BREAKOUT_ROWS; row++) {
        for (col = 0u; col < BREAKOUT_COLS; col++) {
            if (!(s_game.bricks[row] & (1u << col)))
                continue;
            Disp_DrawBox(col * BREAKOUT_BLOCK_XSTEP,
                         BREAKOUT_BLOCK_TOP + row * BREAKOUT_BLOCK_YSTEP,
                         BREAKOUT_BLOCK_W, BREAKOUT_BLOCK_H);
        }
    }
    x = (int16_t)((s_game.paddle_x + BREAKOUT_Q / 2) / BREAKOUT_Q);
    Disp_DrawBox((uint16_t)x, BREAKOUT_PADDLE_Y, BREAKOUT_PADDLE_W, BREAKOUT_PADDLE_H);
    x = (int16_t)((s_game.ball_x + BREAKOUT_Q / 2) / BREAKOUT_Q);
    y = (int16_t)((s_game.ball_y + BREAKOUT_Q / 2) / BREAKOUT_Q);
    if (y >= 0 && y < UI_VER_RES)
        Disp_DrawDisc((uint16_t)x, (uint16_t)y, BREAKOUT_BALL_R, U8G2_DRAW_ALL);

    if (s_game.state == BREAKOUT_READY) {
        state_text = "READY";
        action_text = "CONF: FIRE";
    } else if (s_game.state == BREAKOUT_WON) {
        state_text = "YOU WIN";
        action_text = "CONF: RESTART";
    } else if (s_game.state == BREAKOUT_OVER) {
        state_text = "GAME OVER";
        action_text = "CONF: RETRY";
    } else if (s_game.paused) {
        state_text = "PAUSED";
        action_text = "CONF: RESUME";
    }
    if (state_text) {
        width = Disp_GetUTF8Width(state_text);
        Disp_DrawStr((uint16_t)((UI_HOR_RES - width) / 2u), 39u, state_text);
        /* Use the 12 px menu font and center the entire prompt in one row. */
        Disp_SetFont(UI_FONT);
        width = Disp_GetUTF8Width(action_text);
        Disp_DrawStr((uint16_t)((UI_HOR_RES - width) / 2u), 52u, action_text);
    }
    Disp_SendBuffer();
}

void Game_BreakoutOnLeave(ui_t *ui) {
    (void)ui;
    if (!s_game.active)
        return;
    s_game.active = 0u;
    s_game.step_accum = 0u;
}

void Game_BreakoutRun(ui_t *ui) {
    uint32_t now, elapsed;
    static const uint8_t frame_delay[3] = {17u, 17u, 16u};
    if (!ui)
        return;
    if (ui->action == UI_ACTION_BACK)
        return; /* MiaoUI fade/exit invokes Game_BreakoutOnLeave(). */

    now = Millis();
    if (!s_game.active) {
        s_game.active = 1u;
        breakout_reset(now);
    }
    if (ui->action == UI_ACTION_ENTER) {
        if (s_game.state == BREAKOUT_READY) {
            breakout_launch();
        } else if (s_game.state == BREAKOUT_PLAYING) {
            s_game.paused = (uint8_t)!s_game.paused;
        } else {
            breakout_reset(now);
        }
        s_game.step_accum = 0u;
        s_game.last_ms = now;
    }
    /* All directional menu events are consumed; held keys use KEY_GetState
     * directly and therefore do not depend on the 1 s DAS repeat delay. */
    ui->action = UI_ACTION_NONE;

    UI_RequestNextFrame(ui, frame_delay[s_game.frame_phase]);
    s_game.frame_phase = (uint8_t)((s_game.frame_phase + 1u) % 3u);

    elapsed = (uint32_t)(now - s_game.last_ms);
    s_game.last_ms = now;
    if (elapsed > 50u)
        elapsed = 50u; /* At most three simulation ticks after DMA stalls. */
    s_game.step_accum += elapsed * 60u;
    while (s_game.step_accum >= 1000u) {
        s_game.step_accum -= 1000u;
        breakout_tick();
    }
    breakout_draw();
}
