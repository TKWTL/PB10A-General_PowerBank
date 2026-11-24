/*
 * Tetris, ported from Tiny Tris v3 by Daniel Champagne, 2025 Tinyjoypad release.
 * Adapted from tiny-tris_v3.ino for PB10A.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "game_tetris.h"
#include "tetris_assets.h"
#include "beep_service.h"
#include "buttons.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "millis.h"
#include <stdint.h>
#include <string.h>

#define WELL_COLUMNS 12
#define WELL_ROWS 19
#define WELL_FULL ((1u << WELL_COLUMNS) - 1u)
#define WELL_X 46
#define WELL_Y 5
#define CELL_PIXELS 3
#define PIECE_SIZE 5
#define PIECE_COUNT (sizeof(tetris_pieces) / PIECE_SIZE)
#define WIDTH 128
#define HEIGHT 64
#define FRAME_MS 33u
#define RESUME_GAP_MS 150u
#define MOVE_DELAY_MS 180u
#define MOVE_REPEAT_MS 66u
#define SOFT_DROP_MS 45u
#define CLEAR_FRAMES 10u
#define GAME_OVER_MS 2000u

typedef enum { TETRIS_TITLE,
               TETRIS_PLAYING,
               TETRIS_CLEARING,
               TETRIS_OVER } tetris_phase_t;

typedef struct {
    uint32_t score;
    uint16_t lines;
    uint8_t level;
} tetris_score_t;

typedef struct {
    tetris_score_t score;
    uint16_t well[WELL_ROWS]; /* One bit per column. */
    uint8_t piece[PIECE_SIZE]; /* Rotated copy of the original 5x5 matrix. */
    uint32_t full_lines;
    uint32_t rng;
    uint16_t drop_ms, phase_ms, move_ms;
    int8_t x, y, direction;
    uint8_t type, next, rotation, drop_armed;
    tetris_phase_t phase;
} tetris_game_t;

static tetris_game_t s_game;
static tetris_score_t s_record;
static uint32_t s_last_ms;
static uint8_t s_active;

/* Short versions of the original buzzer cues, submitted without blocking. */
static const uint16_t s_start[] = {2128u, 71u, 3226u, 47u, BEEP_END};
static const uint16_t s_turn[] = {1984u, 3u, 2041u, 5u, 1984u, 3u, BEEP_END};
static const uint16_t s_clear[] = {2326u, 34u, 4762u, 17u, BEEP_END};
static const uint16_t s_over[] = {3226u, 24u, 2326u, 48u, BEEP_END};

static void play(const uint16_t *sequence) {
    if (!beep_busy())
        (void)beep_enqueue(sequence);
}

/* Select_Piece and rotate_Matrix used the middle of the 5x5 matrix as the pivot. */
static void rotate_matrix(uint8_t matrix[PIECE_SIZE], uint8_t type, unsigned rotation) {
    memset(matrix, 0, PIECE_SIZE);
    for (unsigned y = 0; y < PIECE_SIZE; ++y) {
        uint8_t source = tetris_pieces[type * PIECE_SIZE + y];
        for (unsigned x = 0; x < PIECE_SIZE; ++x) {
            if (!(source & (0x80u >> x)))
                continue;
            unsigned col = x, row = y;
            switch (rotation) {
            case 1:
                col = 4u - y;
                row = x;
                break;
            case 2:
                col = 4u - x;
                row = 4u - y;
                break;
            case 3:
                col = y;
                row = 4u - x;
                break;
            default:
                break;
            }
            matrix[row] |= (uint8_t)(1u << col);
        }
    }
}

static uint8_t fits(const tetris_game_t *game, const uint8_t matrix[PIECE_SIZE], int x, int y) {
    for (int row = 0; row < PIECE_SIZE; ++row) {
        for (int col = 0; col < PIECE_SIZE; ++col) {
            if (!(matrix[row] & (1u << col)))
                continue;
            int board_x = x + col, board_y = y + row;
            if (board_x < 0 || board_x >= WELL_COLUMNS || board_y >= WELL_ROWS)
                return 0u;
            if (board_y >= 0 && (game->well[board_y] & (1u << board_x)))
                return 0u;
        }
    }
    return 1u;
}

static void try_rotate(tetris_game_t *game) {
    static const uint8_t rotations[] = {4u, 1u, 2u, 2u, 2u, 4u, 4u};
    unsigned rotation = game->rotation + 1u;
    if (rotation == rotations[game->type])
        rotation = 0u;
    uint8_t matrix[PIECE_SIZE];
    rotate_matrix(matrix, game->type, rotation);
    if (fits(game, matrix, game->x, game->y)) {
        memcpy(game->piece, matrix, sizeof(game->piece));
        game->rotation = (uint8_t)rotation;
        play(s_turn);
    }
}

static uint8_t pick_piece(tetris_game_t *game) {
    uint32_t value = game->rng;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->rng = value;
    return (uint8_t)(value % PIECE_COUNT);
}

static void end_game(tetris_game_t *game) {
    if (game->score.score > s_record.score)
        s_record = game->score;
    game->phase = TETRIS_OVER;
    game->phase_ms = 0u;
    play(s_over);
}

static void spawn(tetris_game_t *game, uint8_t soft_drop) {
    game->type = game->next;
    game->next = pick_piece(game);
    game->rotation = 0u;
    game->x = 3;
    game->y = -1;
    game->drop_ms = 0u;
    game->drop_armed = (uint8_t)!soft_drop;
    rotate_matrix(game->piece, game->type, 0u);
    if (game->well[1] || !fits(game, game->piece, game->x, game->y)) {
        end_game(game);
        return;
    }
    game->phase = TETRIS_PLAYING;
}

static void start_game(tetris_game_t *game, uint32_t now, uint8_t soft_drop) {
    memset(game, 0, sizeof(*game));
    game->rng = now | 1u;
    game->next = pick_piece(game);
    spawn(game, soft_drop);
    play(s_start);
}

static void lock_piece(tetris_game_t *game, uint8_t soft_drop) {
    for (int row = 0; row < PIECE_SIZE; ++row) {
        for (int col = 0; col < PIECE_SIZE; ++col) {
            if (!(game->piece[row] & (1u << col)))
                continue;
            int y = game->y + row;
            if (y >= 0)
                game->well[y] |= (uint16_t)(1u << (game->x + col));
        }
    }
    game->score.score += game->y < 9 ? 2u : 1u;
    game->full_lines = 0u;
    for (unsigned row = 0; row < WELL_ROWS; ++row)
        if (game->well[row] == WELL_FULL)
            game->full_lines |= 1u << row;

    if (game->full_lines) {
        game->phase = TETRIS_CLEARING;
        game->phase_ms = 0u;
        play(s_clear);
    } else {
        spawn(game, soft_drop);
    }
}

static void clear_lines(tetris_game_t *game, uint8_t soft_drop) {
    static const uint8_t line_score[] = {0u, 2u, 5u, 8u, 12u};
    int dst = WELL_ROWS - 1;
    unsigned removed = 0u;
    for (int src = WELL_ROWS - 1; src >= 0; --src) {
        if (game->full_lines & (1u << src))
            ++removed;
        else
            game->well[dst--] = game->well[src];
    }
    while (dst >= 0)
        game->well[dst--] = 0u;
    game->score.lines += removed;
    game->score.score += line_score[removed];
    unsigned level = game->score.lines / 20u;
    game->score.level = level > 99u ? 99u : (uint8_t)level;
    spawn(game, soft_drop);
}

static void move_lateral(tetris_game_t *game, int direction, uint16_t elapsed) {
    if (direction != game->direction) {
        game->direction = (int8_t)direction;
        game->move_ms = 0u;
    } else if (game->move_ms > elapsed) {
        game->move_ms -= elapsed;
        return;
    }
    if (direction && fits(game, game->piece, game->x + direction, game->y))
        game->x += (int8_t)direction;
    game->move_ms = game->move_ms == 0u ? MOVE_DELAY_MS : MOVE_REPEAT_MS;
    if (!direction)
        game->move_ms = 0u;
}

static void advance(tetris_game_t *game, uint16_t elapsed, int direction, uint8_t soft_drop) {
    switch (game->phase) {
    case TETRIS_TITLE:
        game->phase_ms = (uint16_t)((game->phase_ms + elapsed) % (8u * FRAME_MS));
        return;
    case TETRIS_OVER:
        game->phase_ms += elapsed;
        if (game->phase_ms >= GAME_OVER_MS) {
            game->phase = TETRIS_TITLE;
            game->phase_ms = 0u;
        }
        return;
    case TETRIS_CLEARING:
        game->phase_ms += elapsed;
        if (game->phase_ms >= CLEAR_FRAMES * FRAME_MS)
            clear_lines(game, soft_drop);
        return;
    case TETRIS_PLAYING:
        break;
    }

    if (!soft_drop)
        game->drop_armed = 1u;
    if (soft_drop && game->drop_armed)
        direction = 0;
    move_lateral(game, direction, elapsed);
    unsigned speed = game->score.level >= 20u ? 1u : 11u - game->score.level / 2u;
    unsigned interval = soft_drop && game->drop_armed ? SOFT_DROP_MS : speed * 48u;
    game->drop_ms += elapsed;
    while (game->drop_ms >= interval) {
        game->drop_ms -= interval;
        if (!fits(game, game->piece, game->x, game->y + 1)) {
            lock_piece(game, soft_drop);
            return;
        }
        ++game->y;
    }
}

/* Native SSD1306 page order. Shift clipped columns across page boundaries. */
static void blit(uint8_t *buf, const uint8_t *sprite, unsigned frame, int x, int y) {
    unsigned width = sprite[0], pages = sprite[1];
    const uint8_t *pixels = sprite + 2u + frame * width * pages;
    for (unsigned row = 0; row < pages; ++row) {
        int top = y + (int)row * 8;
        if (top <= -8 || top >= HEIGHT)
            continue;
        for (unsigned col = 0; col < width; ++col) {
            int screen_x = x + (int)col;
            if (screen_x < 0 || screen_x >= WIDTH)
                continue;
            uint8_t bits = pixels[row * width + col];
            if (top < 0) {
                buf[screen_x] |= (uint8_t)(bits >> -top);
            } else {
                unsigned page = (unsigned)top >> 3, shift = (unsigned)top & 7u;
                unsigned index = page * WIDTH + (unsigned)screen_x;
                buf[index] |= (uint8_t)(bits << shift);
                if (shift && page < HEIGHT / 8u - 1u)
                    buf[index + WIDTH] |= (uint8_t)(bits >> (8u - shift));
            }
        }
    }
}

static void draw_piece(uint8_t *buf, const uint8_t matrix[PIECE_SIZE], const uint8_t *sprite,
                       int step, int x, int y) {
    for (int row = 0; row < PIECE_SIZE; ++row)
        for (int col = 0; col < PIECE_SIZE; ++col)
            if (matrix[row] & (1u << col))
                blit(buf, sprite, 0u, x + col * step, y + row * step);
}

static void draw_number(uint8_t *buf, uint32_t value, int digits, int x, int y, int spacing) {
    for (int pos = digits - 1; pos >= 0; --pos) {
        blit(buf, tetris_digits, value % 10u, x + spacing * pos, y);
        value /= 10u;
    }
}

static void draw_score(uint8_t *buf, const tetris_score_t *score) {
    draw_number(buf, score->lines, 3, 16, 8, 4);
    draw_number(buf, score->score, 5, 95, 8, 4);
    /* The original panel presents the internal score in tens. */
    blit(buf, tetris_digits, 0u, 115, 8);
    draw_number(buf, score->level, 2, 109, 41, 5);
}

static void render(const tetris_game_t *game) {
    uint8_t *buf = Disp_GetBufferPtr();
    if (!buf)
        return;
    memcpy(buf, tetris_background, sizeof(tetris_background));
    if (game->phase == TETRIS_TITLE) {
        for (unsigned page = 0; page < HEIGHT / 8u; ++page)
            for (unsigned x = 0; x < 36u; ++x)
                buf[page * WIDTH + WELL_X + x] |= tetris_castle[page * 36u + x];
        if (game->phase_ms >= 4u * FRAME_MS) {
            blit(buf, tetris_start_top, 0u, 49, 28);
            blit(buf, tetris_start_bottom, 0u, 49, 36);
        }
        draw_score(buf, &s_record);
    } else {
        for (int y = 0; y < WELL_ROWS; ++y) {
            if (game->phase == TETRIS_CLEARING && ((game->phase_ms / FRAME_MS) & 1u) &&
                (game->full_lines & (1u << y)))
                continue;
            for (int x = 0; x < WELL_COLUMNS; ++x)
                if (game->well[y] & (1u << x))
                    blit(buf, tetris_fixed_block, 0u, WELL_X + x * CELL_PIXELS, WELL_Y + y * CELL_PIXELS);
        }
        if (game->phase == TETRIS_PLAYING) {
            draw_piece(buf, game->piece, tetris_active_block, CELL_PIXELS,
                       WELL_X + game->x * CELL_PIXELS, WELL_Y + game->y * CELL_PIXELS - 5);
        }
        static const int8_t next_dx[] = {1, 0, 1, 0, 1, 1, 0};
        static const int8_t next_dy[] = {1, -1, 0, 0, 1, 0, 0};
        uint8_t preview[PIECE_SIZE];
        rotate_matrix(preview, game->next, 0u);
        draw_piece(buf, preview, tetris_preview_block, 2, 92 + next_dx[game->next], 22 + next_dy[game->next]);
        draw_score(buf, &game->score);
    }
    Disp_SendBuffer();
}

static uint8_t held(KeyIndex_t key) {
    KeyState_t state = KEY_GetState(key);
    return (uint8_t)(state == KeyState_ShortPress || state == KeyState_LongPress);
}

void Game_TetrisOnLeave(ui_t *ui) {
    (void)ui;
    s_active = 0u;
}

void Game_TetrisRun(ui_t *ui) {
    if (!ui || ui->action == UI_ACTION_BACK)
        return;

    uint32_t now = Millis();
    if (!s_active) {
        s_active = 1u;
        s_last_ms = now;
        s_game.phase = TETRIS_TITLE;
        s_game.phase_ms = 0u;
    }

    uint8_t soft_drop = held(KeyIndex_DOWN);
    if (ui->action == UI_ACTION_ENTER) {
        if (s_game.phase == TETRIS_TITLE)
            start_game(&s_game, now, soft_drop);
        else if (s_game.phase == TETRIS_PLAYING)
            try_rotate(&s_game);
    }
    ui->action = UI_ACTION_NONE;

    uint32_t elapsed = now - s_last_ms;
    s_last_ms = now;
    if (!held(KeyIndex_CONF)) {
        if (elapsed > RESUME_GAP_MS)
            elapsed = 0u;
        int direction = (int)held(KeyIndex_RIGHT) - (int)held(KeyIndex_LEFT);
        advance(&s_game, (uint16_t)elapsed, direction, soft_drop);
    }
    render(&s_game);
    UI_RequestNextFrame(ui, FRAME_MS);
}
