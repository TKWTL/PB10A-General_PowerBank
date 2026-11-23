/* Single shared 1024-byte SRAM asset cache for one active UI game. */
#ifndef PB10A_GAME_SHARED_H
#define PB10A_GAME_SHARED_H
#include <stdint.h>
#define GAME_BUFFER_SIZE 1024u
typedef enum {
    GAME_OWNER_NONE = 0u,
    GAME_OWNER_PACMAN = 1u,
    GAME_OWNER_BOMBER = 2u,
    GAME_OWNER_INVADERS = 3u
} game_owner_t;
/* Acquire is idempotent for current owner and does not clear the buffer.
 * A different owner cannot acquire until the current one calls Release.
 * Neither STOP1 nor ordinary render calls release the cache. */
uint8_t *GameBuffer_Acquire(game_owner_t owner);
void GameBuffer_Release(game_owner_t owner);
game_owner_t GameBuffer_Owner(void);
#endif
