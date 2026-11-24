/* Tiny Bomber by Daniel C (2018-2021), GPL-3.0-or-later.
 * Portable gameplay with original pixel masks, levels and frame cadence. */
#ifndef PB10A_BOMBER_CORE_H
#define PB10A_BOMBER_CORE_H
#include <stdint.h>
/* Original 3x3 fire art repeats its outer cells to form a 5x5 cross. */
#define BOMBER_FIRE_RADIUS_CELLS 2u

typedef enum {
    BOMBER_UP = 0, BOMBER_DOWN, BOMBER_LEFT, BOMBER_RIGHT, BOMBER_NONE
} bomber_direction_t;
typedef enum {
    BOMBER_READY = 0, BOMBER_PLAYING, BOMBER_DYING,
    BOMBER_CLEARED, BOMBER_GAME_OVER
} bomber_phase_t;
typedef enum {
    BOMBER_SOUND_NONE = 0, BOMBER_SOUND_START, BOMBER_SOUND_PLACE,
    BOMBER_SOUND_BLAST, BOMBER_SOUND_DEATH, BOMBER_SOUND_LEVEL
} bomber_sound_t;

typedef struct {
    uint8_t anim, dead, type, direction_anim, dir_h, dir_v;
    int16_t x;
    int8_t page_y;
    uint8_t offset_y, switch_anim;
} bomber_actor_t;

typedef struct {
    int16_t x, page_y;  /* -10 means no bomb, otherwise 8px aligned. */
    uint8_t fuse, anim;
} bomber_bomb_t;

typedef struct {
    bomber_actor_t actors[5];
    bomber_bomb_t bomb;
    uint8_t blocks[14]; /* 105 map cells; MSB first, last 7 bits unused. */
    uint8_t blast_reach[4]; /* Up, down, left, right; 0..radius cells. */
    uint8_t blast_ready; /* Snapshot blockers once before any block damage. */
    uint8_t lives, level, speed_bomber, frame, phase, delay_ticks;
    uint8_t sound_event;
    uint32_t random_state;
} bomber_game_t;

/* Bind 1024-byte fixed-wall map for the active game; unbind on page leave. */
void Bomber_CoreBindBack(const uint8_t *collision_map);
void Bomber_CoreNewGame(bomber_game_t *g, uint32_t seed);
void Bomber_CoreStart(bomber_game_t *g);
void Bomber_CoreTick(bomber_game_t *g, bomber_direction_t input, uint8_t place);
uint8_t Bomber_CoreBlockVisible(const bomber_game_t *g, int16_t index);
uint8_t Bomber_CoreAllEnemiesDead(const bomber_game_t *g);
/* True only for cells reached by current flame, including the center.
 * Destructible blocks stop the ray at their own cell; fixed walls are
 * neither burned nor traversed. Rendering and damage share this mask. */
uint8_t Bomber_CoreFireCell(const bomber_game_t *g, int8_t dx, int8_t dy);
uint8_t Bomber_CoreCollisionBack(const bomber_game_t *g, uint8_t actor_index,
                                 uint8_t horizontal);
bomber_sound_t Bomber_CoreTakeSound(bomber_game_t *g);
#endif
