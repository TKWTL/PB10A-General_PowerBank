#include "../pacman_core.h"
#include "../pacman_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Regression CRC32 fingerprints computed from supplied original spritebank.h. */
static uint32_t asset_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0u; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0u; bit < 8u; bit++)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

int main(void) {
    pacman_game_t g;
    size_t i;
    unsigned n = 0;
    assert(sizeof(pacman_BackBlitz) == 1024u);
    assert(sizeof(pacman_back) == 1024u);
    assert(sizeof(pacman_dots) == 1024u);
    assert(sizeof(pacman_caracters) == 192u);
    assert(sizeof(pacman_Music) == 140u);
    assert(asset_crc32(pacman_BackBlitz, 1024u) == 0x7df30593u);
    assert(asset_crc32(pacman_back, 1024u) == 0x63bc13e4u);
    assert(asset_crc32(pacman_dots, 1024u) == 0x73738c64u);
    assert(asset_crc32(pacman_caracters, 192u) == 0x0ce2d60fu);
    assert(asset_crc32(pacman_fruits, 32u) == 0x36bedb03u);
    assert(asset_crc32(pacman_Music, 140u) == 0x2fe77b41u);
    for (i=0;i<1024u;i++) if (pacman_dots[i]) ++n;
    assert(n == 64u);
    Pacman_CoreNewGame(&g, 1u);
    assert(g.phase == PACMAN_READY && g.lives == 3u && g.dots_left == 64u);
    assert(g.level_speed == 200u && g.level == 1u);
    for(i=0;i<64u;i++) assert(Pacman_CoreDotVisible(&g,(uint8_t)i));
    assert(!Pacman_CoreDotVisible(&g, 64u));
    Pacman_CoreStart(&g);
    assert(g.phase == PACMAN_PLAYING);
    Pacman_CoreTick(&g, PACMAN_RIGHT);
    assert(g.frame == 1u);
    assert(g.actors[0].dir_v == 1u || g.actors[0].dir_v == 2u);
    for(i=0;i<2000u && g.phase != PACMAN_GAME_OVER;i++) {
        Pacman_CoreTick(&g,(pacman_direction_t)((i/47u)%5u));
        (void)Pacman_CoreTakeSound(&g);
        assert(g.dots_left <= 64u && g.lives <= 3u);
        assert(g.level_speed >= 10u);
        assert(g.actors[0].offset_y < 8u);
        assert(g.actors[0].page_y >= -1 && g.actors[0].page_y <= 8);
    }
    Pacman_CoreNewGame(&g,1u);
    assert(g.lives == 3u && g.dots_left == 64u);
    /* Death count: third collision is GAME_OVER, not a fourth life. */
    for(i=0;i<3u;i++) {
        Pacman_CoreStart(&g);
        g.actors[1] = g.actors[0];
        g.actors[1].type = 1u;
        g.actors[1].guber = 0u;
        Pacman_CoreTick(&g,PACMAN_NONE);
        if(i < 2u) {
            assert(g.phase == PACMAN_DYING);
            assert(g.lives == 2u - i);
            for(unsigned j=0;j<18u;j++) Pacman_CoreTick(&g,PACMAN_NONE);
            assert(g.phase == PACMAN_PLAYING);
        } else assert(g.phase == PACMAN_GAME_OVER && g.lives == 0u);
    }
    Pacman_CoreStart(&g);
    assert(g.phase == PACMAN_PLAYING && g.lives == 3u);
    /* Clearing the final dot never accesses an out-of-bounds bit. */
    for(i=0;i<9u;i++) g.dots_mem[i] = 0u;
    g.dots_left = 0u;
    Pacman_CoreTick(&g,PACMAN_NONE);
    assert(g.phase == PACMAN_CLEARED);
    for(i=0;i<44u;i++) Pacman_CoreTick(&g,PACMAN_NONE);
    assert(g.level == 2u && g.level_speed == 190u && g.dots_left == 64u);
    assert(g.phase == PACMAN_PLAYING);
    puts("core game, 3 lives, map/asset arrays, level progression PASS");
    return 0;
}
