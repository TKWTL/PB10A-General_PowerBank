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
#include "mini_format.h"
#include <stdint.h>
#include <string.h>

#define WELL_COLUMNS 12
#define WELL_ROWS 19
#define WELL_FULL ((1u << WELL_COLUMNS) - 1u)
#define WELL_X 2
#define WELL_LEFT (WELL_X - 2)
#define WELL_Y 1
#define CELL_PIXELS 5
#define BLOCK_PIXELS 4
#define WELL_RIGHT (WELL_X + WELL_COLUMNS * CELL_PIXELS)
#define WELL_BOTTOM (WELL_Y + WELL_ROWS * CELL_PIXELS)
#define GAME_OVER_ROW 1
#define GAME_OVER_Y (WELL_Y + (GAME_OVER_ROW + 1) * CELL_PIXELS - 1)
#define HUD_MARGIN 4
#define WIDTH 64
#define HEIGHT 128
#define NEXT_SIZE 20
#define NEXT_X (WIDTH - HUD_MARGIN - NEXT_SIZE)
#define NEXT_Y (HEIGHT - NEXT_SIZE)
#define PIECE_SIZE 5
#define PIECE_COUNT (sizeof(tetris_piece_offsets) - 1u)
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
static uint8_t s_active, s_rotate_held;

/* Short versions of the original buzzer cues, submitted without blocking. */
static const uint16_t s_start[] = {2128u, 71u, 3226u, 47u, BEEP_END};
static const uint16_t s_turn[] = {1984u, 3u, 2041u, 5u, 1984u, 3u, BEEP_END};
static const uint16_t s_clear[] = {2326u, 34u, 4762u, 17u, BEEP_END};
static const uint16_t s_over[] = {3226u, 24u, 2326u, 48u, BEEP_END};

static void play(const uint16_t *sequence) {
    if (!beep_busy())
        (void)beep_enqueue(sequence);
}

/* Zero means a nonempty source row would cross either side of the well. */
static uint16_t project_row(uint8_t row, int x) {
    if (x < 0) {
        unsigned shift = (unsigned)-x;
        if (shift >= PIECE_SIZE || (row & ((1u << shift) - 1u)))
            return 0u;
        return (uint16_t)(row >> shift);
    }
    if (x >= WELL_COLUMNS)
        return 0u;
    uint32_t mask = (uint32_t)row << x;
    return mask <= WELL_FULL ? (uint16_t)mask : 0u;
}

static uint8_t fits(const tetris_game_t *game, const uint8_t matrix[PIECE_SIZE], int x, int y) {
    for (int row = 0; row < PIECE_SIZE; ++row) {
        if (!matrix[row])
            continue;
        int board_y = y + row;
        uint16_t mask = project_row(matrix[row], x);
        if (!mask || board_y >= WELL_ROWS)
            return 0u;
        if (board_y >= 0 && (game->well[board_y] & mask))
            return 0u;
    }
    return 1u;
}

static void try_rotate(tetris_game_t *game, uint8_t clockwise) {
    unsigned first = tetris_piece_offsets[game->type];
    unsigned end = tetris_piece_offsets[game->type + 1u];
    unsigned rotation = game->rotation;
    if (clockwise)
        rotation = rotation + 1u == end ? first : rotation + 1u;
    else
        rotation = rotation == first ? end - 1u : rotation - 1u;
    if (fits(game, tetris_pieces[rotation], game->x, game->y)) {
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
    game->rotation = tetris_piece_offsets[game->type];
    game->x = 3;
    game->y = -1;
    game->drop_ms = 0u;
    game->drop_armed = (uint8_t)!soft_drop;
    if (game->well[GAME_OVER_ROW] || !fits(game, tetris_pieces[game->rotation], game->x, game->y)) {
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
    const uint8_t *matrix = tetris_pieces[game->rotation];
    for (int row = 0; row < PIECE_SIZE; ++row) {
        int y = game->y + row;
        if (matrix[row] && y >= 0)
            game->well[y] |= project_row(matrix[row], game->x);
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
    if (direction && fits(game, tetris_pieces[game->rotation], game->x + direction, game->y))
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
        if (!fits(game, tetris_pieces[game->rotation], game->x, game->y + 1)) {
            lock_piece(game, soft_drop);
            return;
        }
        ++game->y;
    }
}

static void draw_row(uint16_t bits, int x, int y, uint8_t active) {
    while (bits) {
        if (bits & 1u) {
            if (active)
                Disp_DrawBox(x, y, BLOCK_PIXELS, BLOCK_PIXELS);
            else
                Disp_DrawFrame(x, y, BLOCK_PIXELS, BLOCK_PIXELS);
        }
        bits >>= 1u;
        x += CELL_PIXELS;
    }
}

static void draw_preview(uint8_t type) {
    const uint8_t *matrix = tetris_pieces[tetris_piece_offsets[type]];
    unsigned top = 0u, bottom = PIECE_SIZE - 1u;
    while (!matrix[top])
        ++top;
    while (!matrix[bottom])
        --bottom;

    unsigned columns = 0u;
    for (unsigned row = top; row <= bottom; ++row)
        columns |= matrix[row];
    unsigned left = 0u, right = PIECE_SIZE - 1u;
    while (!(columns & (1u << left)))
        ++left;
    while (!(columns & (1u << right)))
        --right;

    int width = (int)(right - left) * CELL_PIXELS + BLOCK_PIXELS;
    int height = (int)(bottom - top) * CELL_PIXELS + BLOCK_PIXELS;
    int x = NEXT_X + (NEXT_SIZE - width) / 2 - (int)left * CELL_PIXELS;
    int y = NEXT_Y + (NEXT_SIZE - height) / 2;
    for (unsigned row = top; row <= bottom; ++row, y += CELL_PIXELS)
        draw_row(matrix[row], x, y, 1u);
}

static void draw_score(const tetris_score_t *score) {
    char line[10];
    Disp_SetFont(font_home_h6w4);
    mini_snprintf(line, sizeof(line), "LINES %03u", (unsigned)(score->lines % 1000u));
    Disp_DrawStr(HUD_MARGIN, 110, line);
    mini_snprintf(line, sizeof(line), "LEVEL %02u", (unsigned)score->level);
    Disp_DrawStr(HUD_MARGIN, 123, line);
}

static void draw_centered(unsigned y, const char *text) {
    Disp_DrawStr((WIDTH - Disp_GetUTF8Width(text)) / 2u, y, text);
}

static void render(const tetris_game_t *game) {
    uint8_t white = 1u;
    Disp_ClearBuffer();
    Disp_SetMaxClipWindow();
    Disp_SetDrawColor(&white);
    if (game->phase == TETRIS_TITLE) {
        char line[13];
        Disp_DrawFrame(WELL_LEFT, WELL_Y - 1, WELL_RIGHT - WELL_LEFT + 1, WELL_BOTTOM + 1);
        Disp_SetFont(UI_FONT);
        draw_centered(23, "TETRIS");
        Disp_SetFont(font_home_h6w4);
        draw_centered(45, "RECORD");
        /* The original panel presents the internal score in tens. */
        mini_snprintf(line, sizeof(line), "SCORE %05u0", (unsigned)(s_record.score % 100000u));
        draw_centered(55, line);
        if (game->phase_ms >= 4u * FRAME_MS) {
            Disp_SetFont(UI_FONT);
            draw_centered(80, "START");
        }
        draw_score(&s_record);
    } else {
        Disp_DrawLine(WELL_LEFT, 0, WELL_LEFT, WELL_BOTTOM);
        Disp_DrawLine(WELL_RIGHT, 0, WELL_RIGHT, WELL_BOTTOM);
        Disp_DrawLine(WELL_LEFT, WELL_BOTTOM, WELL_RIGHT, WELL_BOTTOM);
        /* Mark the bottom of the top-out zone in the inter-row gap. */
        for (unsigned x = WELL_X; x < WELL_RIGHT; x += CELL_PIXELS)
            Disp_DrawLine(x + 1, GAME_OVER_Y, x + 2, GAME_OVER_Y);
        for (int y = 0; y < WELL_ROWS; ++y) {
            if (game->phase == TETRIS_CLEARING && ((game->phase_ms / FRAME_MS) & 1u) &&
                (game->full_lines & (1u << y)))
                continue;
            draw_row(game->well[y], WELL_X, WELL_Y + y * CELL_PIXELS, 0u);
        }
        if (game->phase == TETRIS_PLAYING) {
            const uint8_t *matrix = tetris_pieces[game->rotation];
            for (int row = 0; row < PIECE_SIZE; ++row) {
                int y = game->y + row;
                if (y >= 0)
                    draw_row(matrix[row], WELL_X + game->x * CELL_PIXELS, WELL_Y + y * CELL_PIXELS, 1u);
            }
        }
        draw_score(&game->score);
        Disp_DrawStr(NEXT_X + 2, 105, "NEXT");
        draw_preview(game->next);
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
    /* Same physical 90-degree CCW orientation as Pac-Man. */
    uint8_t rotate_up = held(KeyIndex_RIGHT);
    if (!s_active) {
        s_active = 1u;
        s_rotate_held = rotate_up;
        s_last_ms = now; /* Resume the retained phase; BSS starts at TETRIS_TITLE. */
    }

    /* R1 maps screen right/down/up to native DOWN/LEFT/RIGHT. */
    uint8_t soft_drop = held(KeyIndex_LEFT);
    uint8_t pause = held(KeyIndex_CONF);
    if (ui->action == UI_ACTION_ENTER) {
        if (s_game.phase == TETRIS_TITLE)
            start_game(&s_game, now, soft_drop);
        else if (s_game.phase == TETRIS_PLAYING)
            try_rotate(&s_game, 1u);
    } else if (rotate_up && !s_rotate_held && !pause && s_game.phase == TETRIS_PLAYING) {
        try_rotate(&s_game, 0u);
    }
    s_rotate_held = rotate_up;
    ui->action = UI_ACTION_NONE;

    uint32_t elapsed = now - s_last_ms;
    s_last_ms = now;
    if (!pause) {
        if (elapsed > RESUME_GAP_MS)
            elapsed = 0u;
        int direction = (int)held(KeyIndex_DOWN) - (int)held(KeyIndex_UP);
        advance(&s_game, (uint16_t)elapsed, direction, soft_drop);
    }
    render(&s_game);
    UI_RequestNextFrame(ui, FRAME_MS);
}
