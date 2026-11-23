#include "game_shared.h"
#include <string.h>

static uint8_t s_game_buffer[GAME_BUFFER_SIZE];
static game_owner_t s_game_owner;

uint8_t *GameBuffer_Acquire(game_owner_t owner) {
    if (owner < GAME_OWNER_PACMAN || owner > GAME_OWNER_INVADERS ||
        (s_game_owner != GAME_OWNER_NONE && s_game_owner != owner))
        return 0;
    s_game_owner = owner;
    return s_game_buffer;
}
void GameBuffer_Release(game_owner_t owner) {
    if (owner == GAME_OWNER_NONE || owner != s_game_owner) return;
    memset(s_game_buffer, 0, sizeof(s_game_buffer));
    s_game_owner = GAME_OWNER_NONE;
}
game_owner_t GameBuffer_Owner(void) {
    return s_game_owner;
}
