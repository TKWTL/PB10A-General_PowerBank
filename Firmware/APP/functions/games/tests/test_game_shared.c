#include "../game_shared.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint8_t *p = GameBuffer_Acquire(GAME_OWNER_PACMAN);
    assert(p && GameBuffer_Owner()==GAME_OWNER_PACMAN);
    for (unsigned i=0;i<1024u;i++) p[i]=(uint8_t)i;
    assert(GameBuffer_Acquire(GAME_OWNER_PACMAN)==p);
    assert(GameBuffer_Acquire(GAME_OWNER_BOMBER)==0);
    GameBuffer_Release(GAME_OWNER_BOMBER); /* Wrong owner cannot clear. */
    assert(GameBuffer_Owner()==GAME_OWNER_PACMAN && p[17]==17u);
    /* Simulated STOP1 gap: no release, the cache retains data. */
    assert(GameBuffer_Acquire(GAME_OWNER_PACMAN)[17]==17u);
    GameBuffer_Release(GAME_OWNER_PACMAN);
    assert(GameBuffer_Owner()==GAME_OWNER_NONE);
    for (unsigned i=0;i<1024u;i++) assert(p[i]==0u);
    assert(GameBuffer_Acquire(GAME_OWNER_BOMBER)==p);
    p[127]=0xabu;
    GameBuffer_Release(GAME_OWNER_INVADERS); /* Wrong owner again. */
    assert(p[127]==0xabu);
    GameBuffer_Release(GAME_OWNER_BOMBER);
    assert(p[127]==0u && GameBuffer_Owner()==GAME_OWNER_NONE);
    assert(GameBuffer_Acquire(GAME_OWNER_NONE)==0);
    puts("shared game SRAM cache ownership, STOP1 persistence, reset PASS");
    return 0;
}
