#include "../pacman_core.h"
#include "../pacman_assets.h"
#include "rle.h"
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

/* Exercise a true intersection from the original bitmapped walls. This
 * test searches for an 8+ pixel corridor in which UP is initially blocked,
 * then becomes possible while moving to the right. */
static uint8_t can_step(const pacman_actor_t *p, pacman_direction_t dir) {
    pacman_actor_t q = *p;
    uint8_t hor;
    q.dir_h=q.dir_v=2u;
    switch (dir) {
    case PACMAN_UP:
        q.dir_h=0u;
        if (q.offset_y) --q.offset_y;
        else { q.offset_y=7u; --q.page_y; if(q.page_y==-2)q.page_y=8; }
        hor=0u;break;
    case PACMAN_DOWN:
        q.dir_h=1u;
        if (q.offset_y<7u) ++q.offset_y;
        else {q.offset_y=0u;++q.page_y;if(q.page_y==9)q.page_y=-1;}
        hor=0u;break;
    case PACMAN_LEFT:
        q.dir_v=0u;if(q.page_y!=3||q.x!=86u)--q.x;hor=1u;break;
    case PACMAN_RIGHT:
        q.dir_v=1u;++q.x;hor=1u;break;
    default:return 0u;
    }
    return (uint8_t)!Pacman_CoreCollisionBack(&q,hor);
}

static void test_queued_turns(void) {
    pacman_game_t g;
    pacman_actor_t *p;
    uint8_t found=0u;
    uint8_t x,y,start_x=0u,start_y=0u,turn_x=0u;
    Pacman_CoreNewGame(&g,13u);
    Pacman_CoreStart(&g);
    p=&g.actors[0];
    for(unsigned i=1u;i<5u;i++){
        g.actors[i].x=200u;
        g.actors[i].page_y=7;
        g.actors[i].dir_h=g.actors[i].dir_v=2u;
        g.actors[i].guber=1u;
    }
    for (y=16u;y<48u&&!found;y++) {
        for (x=18u;x<100u&&!found;x++) {
            p->x=x;
            p->page_y=(int8_t)(y>>3);
            p->offset_y=(uint8_t)(y&7u);
            p->dir_v=1u;
            p->dir_h=2u;
            if (can_step(p,PACMAN_UP)) continue;
            for (uint8_t dist=1u;dist<18u;dist++) {
                p->x=(uint8_t)(x+dist-1u);
                if (!can_step(p,PACMAN_RIGHT)) break;
                p->x=(uint8_t)(x+dist);
                if (can_step(p,PACMAN_UP) && dist>=3u) {
                    start_x=x;
                    start_y=y;
                    turn_x=(uint8_t)(x+dist);
                    found=1u;
                    break;
                }
            }
        }
    }
    assert(found);
    p->x=start_x;
    p->page_y=(int8_t)(start_y>>3);
    p->offset_y=(uint8_t)(start_y&7u);
    p->dir_v=1u;p->dir_h=2u;
    assert(!can_step(p,PACMAN_UP));
    assert(can_step(p,PACMAN_RIGHT));
    Pacman_CoreQueueDirection(&g,PACMAN_UP);
    assert(g.queued_direction==PACMAN_UP);
    Pacman_CoreTick(&g,PACMAN_NONE);
    assert(g.queued_direction==PACMAN_UP);
    assert(p->dir_v==1u && p->dir_h==2u);
    uint8_t turned=0u;
    for(unsigned i=0u;i<40u;i++) {
        Pacman_CoreTick(&g,PACMAN_NONE);
        if(g.queued_direction==PACMAN_NONE) {
            assert(p->dir_h==0u && p->dir_v==2u);
            assert(p->x==turn_x);
            turned=1u;break;
        }
    }
    assert(turned);
    /* Independent of perpendicular collision, reversal on the same axis is
     * committed on the next tick. */
    Pacman_CoreQueueDirection(&g,PACMAN_DOWN);
    Pacman_CoreTick(&g,PACMAN_NONE);
    assert(g.queued_direction==PACMAN_NONE);
    assert(p->dir_h==1u && p->dir_v==2u);
    Pacman_CoreQueueDirection(&g,PACMAN_LEFT);
    assert(g.queued_direction==PACMAN_LEFT);
    Pacman_CoreQueueDirection(&g,PACMAN_RIGHT);
    assert(g.queued_direction==PACMAN_RIGHT);
    Pacman_CoreClearQueuedDirection(&g);
    assert(g.queued_direction==PACMAN_NONE);
    Pacman_CoreNewGame(&g,1u);
    assert(g.queued_direction==PACMAN_NONE);
}

int main(void) {
    pacman_game_t g;
    uint8_t backdrop[1024], collision[1024], dots[1024] = {0};
    size_t i;
    unsigned n = 0u;
    assert(sizeof(pacman_BackBlitz_rle) == 362u);
    assert(sizeof(pacman_back_xor_rle) == 207u);
    assert(sizeof(pacman_dot_records) == 128u);
    assert(sizeof(pacman_dot_masks) == 8u);
    assert(sizeof(pacman_caracters) == 192u);
    assert(sizeof(pacman_Music) == 140u);
    assert(RLE_Decode(pacman_BackBlitz_rle,sizeof(pacman_BackBlitz_rle),
                      backdrop,sizeof(backdrop),RLE_WRITE)==RLE_OK);
    assert(asset_crc32(backdrop,sizeof(backdrop)) == 0x7df30593u);
    memcpy(collision,backdrop,sizeof(backdrop));
    assert(RLE_Decode(pacman_back_xor_rle,sizeof(pacman_back_xor_rle),
                      collision,sizeof(collision),RLE_XOR)==RLE_OK);
    assert(asset_crc32(collision,sizeof(collision)) == 0x63bc13e4u);
    for (i=0u;i<64u;i++) {
        uint16_t d = pacman_dot_records[i];
        uint16_t pos = (uint16_t)(((d >> 7) & 7u)*128u+(d & 127u));
        uint8_t index = (uint8_t)((d >> 10) & 7u);
        assert((d & 0xe000u) == 0u && pos < 1024u);
        assert(dots[pos] == 0u);
        dots[pos] = pacman_dot_masks[index];
        n++;
        if (i==0u || i==1u || i==12u || i==13u) {
            assert((d & 127u)==17u || (d & 127u)==18u ||
                   (d & 127u)==119u || (d & 127u)==120u);
            assert(pacman_dot_masks[index]==0x06u);
        }
        if (i==50u || i==51u || i==62u || i==63u)
            assert(pacman_dot_masks[index]==0xc0u);
    }
    assert(n == 64u);
    assert(asset_crc32(dots,sizeof(dots)) == 0x73738c64u);
    assert(asset_crc32(pacman_caracters,192u) == 0x0ce2d60fu);
    assert(asset_crc32(pacman_fruits,32u) == 0x36bedb03u);
    assert(asset_crc32(pacman_Music,140u) == 0x2fe77b41u);
    Pacman_CoreBindBack(collision);
    test_queued_turns();
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
