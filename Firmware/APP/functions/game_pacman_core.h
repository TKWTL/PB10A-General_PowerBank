#ifndef PB10A_PACMAN_GAME_H
#define PB10A_PACMAN_GAME_H

#include <stdint.h>

/* Game state is static RAM; there is no saved data or heap allocation. */
#define PACMAN_GRID_WIDTH 21u
#define PACMAN_GRID_HEIGHT 11u
#define PACMAN_GHOST_COUNT 4u

typedef enum {
    PACMAN_DIR_NONE = 0,
    PACMAN_DIR_UP,
    PACMAN_DIR_RIGHT,
    PACMAN_DIR_DOWN,
    PACMAN_DIR_LEFT
} pacman_dir_t;

typedef enum {
    PACMAN_READY = 0,
    PACMAN_PLAYING,
    PACMAN_PAUSED,
    PACMAN_DYING,
    PACMAN_WON,
    PACMAN_GAME_OVER
} pacman_phase_t;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t dir;
    uint32_t next_move_ms;
} pacman_ghost_t;

typedef struct {
    /* Only one instance, owned by pacman_game.c. Read-only to the UI. */
    uint8_t cells[PACMAN_GRID_HEIGHT][PACMAN_GRID_WIDTH];
    pacman_ghost_t ghosts[PACMAN_GHOST_COUNT];
    uint32_t score;
    uint32_t power_until_ms;
    uint32_t next_player_ms;
    uint32_t phase_until_ms;
    uint32_t paused_at_ms;
    uint32_t rng_state;
    uint16_t pellets_left;
    uint8_t player_x;
    uint8_t player_y;
    uint8_t facing;
    uint8_t desired_dir;
    uint8_t lives;
    uint8_t level;
    uint8_t ghosts_eaten;
    pacman_phase_t phase;
} pacman_game_t;

void PacmanGame_StartSession(uint32_t now_ms);
void PacmanGame_Update(uint32_t now_ms, pacman_dir_t input, uint8_t confirm);
const pacman_game_t *PacmanGame_Get(void);
uint8_t PacmanGame_PowerActive(uint32_t now_ms);
void PacmanGame_EndSession(void);

#endif
