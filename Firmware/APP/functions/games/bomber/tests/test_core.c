#include "../bomber_core.h"
#include "../bomber_assets.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t crc32(const uint8_t *data, unsigned length) {
    uint32_t crc = 0xffffffffu;
    for (unsigned i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned k = 0; k < 8u; k++)
            crc = (crc >> 1u) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
    return ~crc;
}

static void test_resources(void) {
    assert(sizeof(bomber_BackBlitz) == 1024u);
    assert(sizeof(bomber_back) == 1024u);
    assert(sizeof(bomber_caracters) == 192u);
    assert(sizeof(bomber_fire) == 216u);
    assert(sizeof(bomber_BlocDetect) == 128u);
    assert(sizeof(bomber_level) == 42u);
    assert(sizeof(bomber_bomb) == 24u);
    assert(sizeof(bomber_Music) == 42u);
    assert(crc32(bomber_BackBlitz, 1024u) == 0xa58aefdau);
    assert(crc32(bomber_back, 1024u) == 0x669c74eeu);
    assert(crc32(bomber_caracters, 192u) == 0xfb289dc8u);
    assert(crc32(bomber_fire, 216u) == 0x8308cb8fu);
    assert(crc32(bomber_BlocDetect, 128u) == 0xbf7c6a60u);
    assert(crc32(bomber_level, 42u) == 0x11eef69eu);
    assert(crc32(bomber_bomb, 24u) == 0xcc24ab26u);
    assert(crc32(bomber_Music, 42u) == 0x9bed87d0u);
}

static void test_level_and_blocks(void) {
    bomber_game_t g;
    Bomber_CoreNewGame(&g, 1u);
    assert(g.phase == BOMBER_READY && g.lives == 3u && g.level == 0u);
    assert(memcmp(g.blocks, bomber_level, 14u) == 0);
    assert(!Bomber_CoreBlockVisible(&g,-1));
    assert(!Bomber_CoreBlockVisible(&g,105));
    assert(!Bomber_CoreBlockVisible(&g,106));
    assert(!Bomber_CoreBlockVisible(&g,32767));
    Bomber_CoreStart(&g);
    assert(g.phase == BOMBER_PLAYING);
    for (unsigned level = 0u; level < 3u; level++) {
        for (unsigned enemy = 1u; enemy <= 4u; enemy++) g.actors[enemy].dead = 1u;
        g.frame = 11u;
        Bomber_CoreTick(&g, BOMBER_NONE, 0u);
        assert(g.phase == BOMBER_CLEARED);
        for (unsigned t = 0u; t < 36u; t++) Bomber_CoreTick(&g,BOMBER_NONE,0u);
        assert(g.phase == BOMBER_PLAYING);
        assert(g.level == (level + 1u) % 3u);
        assert(memcmp(g.blocks, bomber_level + g.level * 14u, 14u) == 0);
        assert(g.speed_bomber == (level == 2u));
    }
}

/* Upstream ignores BlocDetect[16]==FE for collision but renders it as
 * column 1. Without that exception, the adjacent row-4 column-1 block
 * stops the spawn-lane player at y=48 although column 0 is empty. */
static void test_spawn_lane_fe_sentinel(void) {
    bomber_game_t g;
    bomber_actor_t *p;
    int16_t y;
    Bomber_CoreNewGame(&g, 1u);
    Bomber_CoreStart(&g);
    assert(bomber_BlocDetect[16] == 0xfeu);
    assert(!Bomber_CoreBlockVisible(&g, 4 * 15));
    assert(Bomber_CoreBlockVisible(&g, 4 * 15 + 1));
    p = &g.actors[0];
    assert(p->x == 9 && p->page_y == 7 && p->offset_y == 0u);
    p->dir_h = 0u; /* Up; test collision after each 1px step. */
    p->dir_v = 2u;
    for (y = 55; y >= 40; --y) {
        p->page_y = (int8_t)(y / 8);
        p->offset_y = (uint8_t)(y % 8);
        assert(!Bomber_CoreCollisionBack(&g, 0u, 0u));
    }
    /* No obstacle has been destroyed to fake successful traversal. */
    assert(Bomber_CoreBlockVisible(&g, 4 * 15 + 1));
}

static void test_bomb_explosion(void) {
    bomber_game_t g;
    Bomber_CoreNewGame(&g, 2u);
    Bomber_CoreStart(&g);
    Bomber_CoreTick(&g, BOMBER_NONE, 1u);
    assert(g.bomb.fuse == 5u && g.bomb.x == 8 && g.bomb.page_y == 7);
    Bomber_CoreTick(&g, BOMBER_NONE, 1u); /* No second bomb. */
    assert(g.bomb.fuse == 5u);
    g.bomb.x = 48;
    g.bomb.page_y = 4;
    g.bomb.fuse = 1u;
    g.bomb.anim = 1u;
    g.frame = 3u;
    memset(g.blocks, 0xff, sizeof(g.blocks));
    Bomber_CoreTick(&g, BOMBER_NONE, 0u); /* Frame 4: single damage event. */
    assert(g.bomb.anim == 2u);
    /* All eight arm cells at radius two must be cleared too. */
    assert(!Bomber_CoreBlockVisible(&g,5 + 1*15));
    assert(!Bomber_CoreBlockVisible(&g,5 + 2*15));
    assert(!Bomber_CoreBlockVisible(&g,5 + 3*15));
    assert(!Bomber_CoreBlockVisible(&g,5 + 4*15));
    assert(!Bomber_CoreBlockVisible(&g,5 + 5*15));
    assert(!Bomber_CoreBlockVisible(&g,3 + 3*15));
    assert(!Bomber_CoreBlockVisible(&g,4 + 3*15));
    assert(!Bomber_CoreBlockVisible(&g,6 + 3*15));
    assert(!Bomber_CoreBlockVisible(&g,7 + 3*15));
    assert(Bomber_CoreBlockVisible(&g,8 + 3*15));
    assert(Bomber_CoreBlockVisible(&g,5 + 0*15));
    assert(Bomber_CoreBlockVisible(&g,5 + 6*15));
    assert(g.phase == BOMBER_PLAYING);
    for (unsigned t = 0u; t < 4u; t++) Bomber_CoreTick(&g, BOMBER_NONE, 0u);
    assert(g.bomb.fuse == 0u);
}

static void test_blast_reaches_two_cells(void) {
    bomber_game_t g;
    Bomber_CoreNewGame(&g, 2u);
    Bomber_CoreStart(&g);
    g.bomb.x = 48;
    g.bomb.page_y = 4;
    g.bomb.fuse = 1u;
    g.bomb.anim = 1u;
    g.frame = 3u;
    /* The four enemies are two full cells away, one in each arm. */
    g.actors[1].x = 48;g.actors[1].page_y = 2;
    g.actors[2].x = 48;g.actors[2].page_y = 6;
    g.actors[3].x = 32;g.actors[3].page_y = 4;
    g.actors[4].x = 64;g.actors[4].page_y = 4;
    for (unsigned i = 1; i <= 4; ++i) {
        g.actors[i].offset_y = 0u;
        g.actors[i].dir_v = g.actors[i].dir_h = 2u;
    }
    Bomber_CoreTick(&g, BOMBER_NONE, 0u);
    for (unsigned i = 1; i <= 4; ++i) assert(g.actors[i].dead);
    assert(g.phase == BOMBER_PLAYING);
}

static void test_life_and_restart(void) {
    bomber_game_t g;
    Bomber_CoreNewGame(&g, 7u);
    Bomber_CoreStart(&g);
    g.blocks[0] ^= 0x80u;
    uint8_t persistent = g.blocks[0];
    for (unsigned death = 0u; death < 3u; death++) {
        g.actors[1].x = g.actors[0].x;
        g.actors[1].page_y = g.actors[0].page_y;
        g.actors[1].offset_y = g.actors[0].offset_y;
        g.actors[1].dead = 0u;
        Bomber_CoreTick(&g, BOMBER_NONE, 0u);
        if (death < 2u) {
            assert(g.phase == BOMBER_DYING && g.lives == 2u-death);
            for (unsigned t=0;t<18u;t++) Bomber_CoreTick(&g,BOMBER_NONE,0u);
            assert(g.phase == BOMBER_PLAYING);
            assert(g.blocks[0] == persistent); /* Same level survives death. */
        } else {
            assert(g.phase == BOMBER_GAME_OVER && g.lives == 0u);
        }
    }
    Bomber_CoreStart(&g);
    assert(g.phase == BOMBER_PLAYING && g.lives == 3u);
}

static void test_random_walk(void) {
    bomber_game_t g;
    Bomber_CoreNewGame(&g, 123u);
    Bomber_CoreStart(&g);
    for (unsigned i=0;i<3000u && g.phase!=BOMBER_GAME_OVER;i++) {
        bomber_direction_t d = (bomber_direction_t)((i/35u)%5u);
        Bomber_CoreTick(&g,d,(uint8_t)((i%97u)==0u));
        (void)Bomber_CoreTakeSound(&g);
        assert(g.lives<=3u && g.level<=2u);
        assert(g.bomb.fuse<=5u && g.bomb.anim<=2u);
        for(unsigned k=0;k<5u;k++){
            assert(g.actors[k].offset_y<8u);
            assert(g.actors[k].x>=0 && g.actors[k].x<=127);
            assert(g.actors[k].page_y>=0 && g.actors[k].page_y<=7);
        }
    }
}

int main(void){
    test_resources();
    test_level_and_blocks();
    test_spawn_lane_fe_sentinel();
    test_bomb_explosion();
    test_blast_reaches_two_cells();
    test_life_and_restart();
    test_random_walk();
    puts("bomber_core: original assets, bit map, levels, fuse, life PASS");
    return 0;
}
