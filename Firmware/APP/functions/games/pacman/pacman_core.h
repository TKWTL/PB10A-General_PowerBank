/* TinyPacman gameplay port. Original by Daniel C 2018-2021, GPLv3 or later.
 * Board logic and 6x7 pixel mask tests intentionally follow tinypacman.ino.
 * All mutable state lives in the caller-owned static pacman_game_t. */
#ifndef PB10A_TINYPACMAN_CORE_H
#define PB10A_TINYPACMAN_CORE_H
#include <stdint.h>

typedef enum {
    PACMAN_UP = 0,
    PACMAN_DOWN,
    PACMAN_LEFT,
    PACMAN_RIGHT,
    PACMAN_NONE
} pacman_direction_t;
typedef enum {
    PACMAN_READY = 0,
    PACMAN_PLAYING,
    PACMAN_DYING,
    PACMAN_CLEARED,
    PACMAN_GAME_OVER
} pacman_phase_t;
typedef enum {
    PACMAN_SOUND_NONE = 0,
    PACMAN_SOUND_START,
    PACMAN_SOUND_DOT,
    PACMAN_SOUND_POWER,
    PACMAN_SOUND_GHOST,
    PACMAN_SOUND_DEATH,
    PACMAN_SOUND_LEVEL
} pacman_sound_t;

typedef struct {
    uint8_t anim, guber, type, direction_anim;
    uint8_t dir_h, dir_v, x, offset_y;
    int8_t page_y;
    uint8_t switch_anim;
} pacman_actor_t;

typedef struct {
    pacman_actor_t actors[5];
    uint8_t dots_mem[9];
    uint8_t level_speed, gobbing_end, lives;
    uint8_t power_active, power_timer, frame;
    uint8_t dots_left, level, phase, sound_event;
    uint8_t delay_ticks;
    uint8_t queued_direction; /* Last not-yet-executable requested turn. */
    uint32_t random_state;
} pacman_game_t;

/* Bind the pre-decompressed 1024-byte collision map for this one game page.
 * Caller must unbind before releasing the shared game resource buffer. */
void Pacman_CoreBindBack(const uint8_t *collision_map);
void Pacman_CoreNewGame(pacman_game_t *g, uint32_t seed);
void Pacman_CoreStart(pacman_game_t *g);
void Pacman_CoreQueueDirection(pacman_game_t *g, pacman_direction_t dir);
void Pacman_CoreClearQueuedDirection(pacman_game_t *g);
void Pacman_CoreTick(pacman_game_t *g, pacman_direction_t dir);
pacman_sound_t Pacman_CoreTakeSound(pacman_game_t *g);
uint8_t Pacman_CoreDotVisible(const pacman_game_t *g, uint8_t number);
uint8_t Pacman_CoreCollisionBack(const pacman_actor_t *actor, uint8_t horizontal);
#endif
