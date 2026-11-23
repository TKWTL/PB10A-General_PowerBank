/* Tiny Invaders v3.1, Daniel C 2018-2021, GPL-3.0-or-later.
 * Portable 128x64 core: rendering has no game-state side effects. */
#ifndef PB10A_INVADERS_CORE_H
#define PB10A_INVADERS_CORE_H
#include <stdint.h>

typedef enum {
    INVADERS_TITLE = 0, INVADERS_READY, INVADERS_PLAYING,
    INVADERS_DYING, INVADERS_LEVEL_CLEAR, INVADERS_GAME_OVER
} invaders_phase_t;
typedef enum {
    INVADERS_SOUND_NONE = 0, INVADERS_SOUND_START, INVADERS_SOUND_SHOT,
    INVADERS_SOUND_HIT, INVADERS_SOUND_DIE, INVADERS_SOUND_LEVEL,
    INVADERS_SOUND_UFO
} invaders_sound_t;
typedef enum {
    INVADERS_NO_MOVE = 0, INVADERS_MOVE_LEFT, INVADERS_MOVE_RIGHT
} invaders_move_t;

typedef struct {
    int8_t monsters[4][6]; /* -1 empty, 0..5 live, 8..11 explosion */
    uint8_t shields[6]; /* 3 groups, two 8-column pieces per group */
    uint8_t lives, level, monster_count;
    uint8_t ship_pos, ship_target, ship_dead;
    int8_t shot_y;       /* -1 means inactive, SSD1306 page position */
    uint8_t shot_x, shot_half, shot_ready;
    uint8_t enemy_shot_x, enemy_shot_half_y; /* 16 means inactive */
    int16_t ufo_x;      /* -120 means inactive */
    uint8_t ship_bg_offset, one_frame, anim;
    uint8_t formation_x, formation_y, formation_y_shift;
    uint8_t formation_right, formation_frame, formation_frame_max;
    uint8_t formation_floor, shield_removed;
    uint8_t enemy_fire_cadence, delay_ticks, death_ticks;
    uint8_t phase, sound_event;
    uint32_t random_state;
} invaders_game_t;

void Invaders_CoreInit(invaders_game_t *g, uint32_t seed);
void Invaders_CoreStart(invaders_game_t *g);
void Invaders_CoreTick(invaders_game_t *g, invaders_move_t move, uint8_t shoot);
uint8_t Invaders_CorePixel(const invaders_game_t *g, uint8_t x, uint8_t page);
void Invaders_CoreRender(const invaders_game_t *g, uint8_t *framebuffer);
invaders_sound_t Invaders_CoreTakeSound(invaders_game_t *g);
#endif
