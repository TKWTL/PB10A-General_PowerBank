/* PB10A Pac-Man-style port, adapted from the MIT-licensed game Ardu-man.
 * Original game by Seth A. Robinson (C) 2016, https://github.com/SethRobinson/arduman
 * This version uses a compact cell maze and deterministic integer physics.
 * All mutable game data stays in one static RAM allocation; no EEPROM/NVM.
 * The original MIT license is supplied in ARDUMAN_LICENSE.txt and should
 * be installed as APP/functions/LICENSE-ARDUMAN.
 */
#include "game_pacman_core.h"
#include <string.h>

#define PACMAN_PLAYER_PERIOD_MS 135u
#define PACMAN_POWER_MS 6000u
#define PACMAN_RECOVER_MS 650u

/* 21x11 maze, including one wrap-around tunnel at y=5. All corridors
 * and pellets are reachable from the central player spawn. */
static const char s_initial_maze[PACMAN_GRID_HEIGHT][PACMAN_GRID_WIDTH + 1u] = {
    "#####################",
    "#o..#.....#.....#..o#",
    "#.#.#.###.#.###.#.#.#",
    "#.#...#.......#...#.#",
    "#.###.#.##.##.#.###.#",
    "..........P..........",
    "#.###.#.##.##.#.###.#",
    "#.#...#.......#...#.#",
    "#.#.#.###.#.###.#.#.#",
    "#o..#.....#.....#..o#",
    "#####################"
};

static pacman_game_t s_game;
static const int8_t s_dx[5] = {0, 0, 1, 0, -1};
static const int8_t s_dy[5] = {0, -1, 0, 1, 0};

static uint32_t game_random(void) {
    uint32_t x = s_game.rng_state;
    if (x == 0u) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_game.rng_state = x;
    return x;
}

static uint8_t move_target(uint8_t x, uint8_t y, uint8_t dir,
                           uint8_t *nx, uint8_t *ny) {
    int16_t xx, yy;
    if (dir < PACMAN_DIR_UP || dir > PACMAN_DIR_LEFT) return 0u;
    xx = (int16_t)x + s_dx[dir];
    yy = (int16_t)y + s_dy[dir];
    if (y == 5u && xx < 0) xx = (int16_t)PACMAN_GRID_WIDTH - 1;
    if (y == 5u && xx >= (int16_t)PACMAN_GRID_WIDTH) xx = 0;
    if (xx < 0 || xx >= (int16_t)PACMAN_GRID_WIDTH || yy < 0 ||
        yy >= (int16_t)PACMAN_GRID_HEIGHT) return 0u;
    if (s_game.cells[yy][xx] == '#') return 0u;
    *nx = (uint8_t)xx;
    *ny = (uint8_t)yy;
    return 1u;
}

static uint8_t opposite(uint8_t dir) {
    switch (dir) {
    case PACMAN_DIR_UP: return PACMAN_DIR_DOWN;
    case PACMAN_DIR_RIGHT: return PACMAN_DIR_LEFT;
    case PACMAN_DIR_DOWN: return PACMAN_DIR_UP;
    case PACMAN_DIR_LEFT: return PACMAN_DIR_RIGHT;
    default: return PACMAN_DIR_NONE;
    }
}

static uint8_t abs_diff(uint8_t a, uint8_t b) {
    return a > b ? (uint8_t)(a - b) : (uint8_t)(b - a);
}

static void reset_positions(uint32_t now_ms) {
    static const uint8_t starts[PACMAN_GHOST_COUNT][2] = {
        {3u, 1u}, {17u, 1u}, {3u, 9u}, {17u, 9u}
    };
    uint8_t i;
    s_game.player_x = 10u;
    s_game.player_y = 5u;
    s_game.facing = PACMAN_DIR_LEFT;
    s_game.desired_dir = PACMAN_DIR_NONE;
    s_game.power_until_ms = 0u;
    s_game.ghosts_eaten = 0u;
    s_game.next_player_ms = now_ms + PACMAN_PLAYER_PERIOD_MS;
    for (i = 0u; i < PACMAN_GHOST_COUNT; ++i) {
        s_game.ghosts[i].x = starts[i][0];
        s_game.ghosts[i].y = starts[i][1];
        s_game.ghosts[i].dir = (i & 1u) ? PACMAN_DIR_LEFT : PACMAN_DIR_RIGHT;
        s_game.ghosts[i].next_move_ms = now_ms + 220u + (uint32_t)i * 55u;
    }
}

static void reset_maze(uint32_t now_ms) {
    uint8_t x, y;
    s_game.pellets_left = 0u;
    for (y = 0u; y < PACMAN_GRID_HEIGHT; ++y) {
        for (x = 0u; x < PACMAN_GRID_WIDTH; ++x) {
            uint8_t c = (uint8_t)s_initial_maze[y][x];
            if (c == 'P') c = ' ';
            if (c == '.' || c == 'o') ++s_game.pellets_left;
            s_game.cells[y][x] = c;
        }
    }
    reset_positions(now_ms);
    s_game.phase = PACMAN_READY;
}

void PacmanGame_StartSession(uint32_t now_ms) {
    memset(&s_game, 0, sizeof(s_game));
    s_game.level = 1u;
    s_game.lives = 3u;
    s_game.rng_state = now_ms ^ 0xAC1DF00Du;
    reset_maze(now_ms);
}

void PacmanGame_EndSession(void) {
    memset(&s_game, 0, sizeof(s_game));
}

const pacman_game_t *PacmanGame_Get(void) {
    return &s_game;
}

uint8_t PacmanGame_PowerActive(uint32_t now_ms) {
    return (uint8_t)(s_game.power_until_ms != 0u &&
        (int32_t)(now_ms - s_game.power_until_ms) < 0);
}

static void eat_pellet(uint32_t now_ms) {
    uint8_t *cell = &s_game.cells[s_game.player_y][s_game.player_x];
    if (*cell == '.') {
        *cell = ' ';
        s_game.score += 10u;
        --s_game.pellets_left;
    } else if (*cell == 'o') {
        *cell = ' ';
        s_game.score += 50u;
        --s_game.pellets_left;
        s_game.power_until_ms = now_ms + PACMAN_POWER_MS;
        s_game.ghosts_eaten = 0u;
    }
    if (!s_game.pellets_left) s_game.phase = PACMAN_WON;
}

static void collide_ghost(uint8_t index, uint32_t now_ms) {
    pacman_ghost_t *g = &s_game.ghosts[index];
    if (s_game.phase != PACMAN_PLAYING || g->x != s_game.player_x ||
        g->y != s_game.player_y) return;
    if (PacmanGame_PowerActive(now_ms)) {
        static const uint16_t points[4] = {200u, 400u, 800u, 1600u};
        uint8_t n = s_game.ghosts_eaten;
        s_game.score += points[n < 4u ? n : 3u];
        if (n < 4u) ++s_game.ghosts_eaten;
        /* Recycle the ghost to its own starting corner. */
        g->x = (index & 1u) ? 17u : 3u;
        g->y = (index & 2u) ? 9u : 1u;
        g->next_move_ms = now_ms + 800u;
    } else {
        --s_game.lives;
        s_game.phase = PACMAN_DYING;
        s_game.phase_until_ms = now_ms + PACMAN_RECOVER_MS;
    }
}

static void move_player(uint32_t now_ms) {
    uint8_t x, y;
    if (s_game.desired_dir != PACMAN_DIR_NONE &&
        move_target(s_game.player_x, s_game.player_y,
                    s_game.desired_dir, &x, &y)) {
        s_game.facing = s_game.desired_dir;
    }
    if (!move_target(s_game.player_x, s_game.player_y,
                     s_game.facing, &x, &y)) return;
    s_game.player_x = x;
    s_game.player_y = y;
    eat_pellet(now_ms);
    for (uint8_t i = 0u; i < PACMAN_GHOST_COUNT; ++i)
        collide_ghost(i, now_ms);
}

static void move_ghost(uint8_t index, uint32_t now_ms) {
    pacman_ghost_t *g = &s_game.ghosts[index];
    uint8_t dirs[4], xs[4], ys[4], count = 0u;
    uint8_t preferred, direction, nx, ny;
    uint16_t best_score = 65535u;
    uint8_t pick = 0u;
    uint8_t frightened = PacmanGame_PowerActive(now_ms);
    uint8_t target_x = s_game.player_x, target_y = s_game.player_y;
    uint32_t random = game_random();

    if (index == 1u) { /* Ambusher: look two cells ahead. */
        uint8_t ahead = s_game.facing;
        uint8_t tx, ty;
        if (move_target(target_x, target_y, ahead, &tx, &ty)) {
            target_x = tx; target_y = ty;
            if (move_target(target_x, target_y, ahead, &tx, &ty)) {
                target_x = tx; target_y = ty;
            }
        }
    } else if (index == 2u && (random & 7u) == 0u) {
        /* Occasional retreat to the far corner. */
        target_x = 1u; target_y = 9u;
    }
    for (direction = PACMAN_DIR_UP; direction <= PACMAN_DIR_LEFT; ++direction) {
        if (!move_target(g->x, g->y, direction, &nx, &ny)) continue;
        dirs[count] = direction;
        xs[count] = nx;
        ys[count] = ny;
        ++count;
    }
    if (!count) return;
    preferred = opposite(g->dir);
    for (uint8_t i = 0u; i < count; ++i) {
        uint16_t dist = (uint16_t)abs_diff(xs[i], target_x) +
                        (uint16_t)abs_diff(ys[i], target_y);
        uint16_t weight = (uint16_t)((random >> (i * 5u)) & 7u);
        uint16_t score;
        if (dirs[i] == preferred && count > 1u) continue;
        /* Four simple personalities; frightened ghosts flee instead. */
        if (frightened) score = (uint16_t)(60u - dist * 2u + weight);
        else if (index == 3u) score = weight;
        else score = (uint16_t)(dist * 4u + weight);
        if (score < best_score) {best_score = score; pick = i;}
    }
    g->x = xs[pick];
    g->y = ys[pick];
    g->dir = dirs[pick];
    collide_ghost(index, now_ms);
}

void PacmanGame_Update(uint32_t now_ms, pacman_dir_t input, uint8_t confirm) {
    uint8_t i;
    if (input != PACMAN_DIR_NONE) s_game.desired_dir = (uint8_t)input;

    if (s_game.phase == PACMAN_READY) {
        if (confirm) {
            s_game.phase = PACMAN_PLAYING;
            s_game.next_player_ms = now_ms + PACMAN_PLAYER_PERIOD_MS;
            for (i = 0u; i < PACMAN_GHOST_COUNT; ++i)
                s_game.ghosts[i].next_move_ms = now_ms + 220u + (uint32_t)i * 55u;
        }
        return;
    }
    if (s_game.phase == PACMAN_PAUSED) {
        if (confirm) {
            uint32_t elapsed = now_ms - s_game.paused_at_ms;
            s_game.next_player_ms += elapsed;
            if (s_game.power_until_ms) s_game.power_until_ms += elapsed;
            for (i = 0u; i < PACMAN_GHOST_COUNT; ++i)
                s_game.ghosts[i].next_move_ms += elapsed;
            s_game.phase = PACMAN_PLAYING;
        }
        return;
    }
    if (s_game.phase == PACMAN_DYING) {
        if ((int32_t)(now_ms - s_game.phase_until_ms) >= 0) {
            if (s_game.lives == 0u) s_game.phase = PACMAN_GAME_OVER;
            else {reset_positions(now_ms); s_game.phase = PACMAN_READY;}
        }
        return;
    }
    if (s_game.phase == PACMAN_WON || s_game.phase == PACMAN_GAME_OVER) {
        if (confirm) {
            if (s_game.phase == PACMAN_WON) {
                if (s_game.level < 99u) ++s_game.level;
                reset_maze(now_ms);
            } else PacmanGame_StartSession(now_ms);
        }
        return;
    }
    if (s_game.phase != PACMAN_PLAYING) return;
    if (confirm) {
        s_game.paused_at_ms = now_ms;
        s_game.phase = PACMAN_PAUSED;
        return;
    }
    if ((int32_t)(now_ms - s_game.next_player_ms) >= 0) {
        s_game.next_player_ms += PACMAN_PLAYER_PERIOD_MS;
        if ((int32_t)(now_ms - s_game.next_player_ms) >= 0)
            s_game.next_player_ms = now_ms + PACMAN_PLAYER_PERIOD_MS;
        move_player(now_ms);
    }
    if (s_game.phase != PACMAN_PLAYING) return;
    for (i = 0u; i < PACMAN_GHOST_COUNT; ++i) {
        uint32_t period = (uint32_t)(235u + i * 22u);
        uint32_t level_bonus = (uint32_t)(s_game.level - 1u) * 6u;
        if (level_bonus > 75u) level_bonus = 75u;
        period -= level_bonus;
        if (PacmanGame_PowerActive(now_ms)) period += 85u;
        if ((int32_t)(now_ms - s_game.ghosts[i].next_move_ms) >= 0) {
            s_game.ghosts[i].next_move_ms += period;
            if ((int32_t)(now_ms - s_game.ghosts[i].next_move_ms) >= 0)
                s_game.ghosts[i].next_move_ms = now_ms + period;
            move_ghost(i, now_ms);
            if (s_game.phase != PACMAN_PLAYING) break;
        }
    }
}
