#include "../invaders_core.h"
#include "../invaders_assets.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t crc32(const uint8_t *p, unsigned length) {
    uint32_t c=0xffffffffu;
    for(unsigned i=0;i<length;i++) {
        c^=p[i];
        for(unsigned k=0;k<8u;k++) c=(c>>1)^((c&1u)?0xedb88320u:0u);
    }
    return ~c;
}
static void test_assets(void) {
    assert(sizeof(invaders_levels)==240u && crc32((const uint8_t*)invaders_levels,240u)==0xd5e7148cu);
    assert(sizeof(invaders_lives)==15u && crc32(invaders_lives,15u)==0xd09304dfu);
    assert(sizeof(invaders_shoot)==2u && crc32(invaders_shoot,2u)==0xc5daeed3u);
    assert(sizeof(invaders_monsters)==168u && crc32(invaders_monsters,168u)==0x49d812e6u);
    assert(sizeof(invaders_ship)==26u && crc32(invaders_ship,26u)==0x43963b5cu);
    assert(sizeof(invaders_back)==1024u && crc32(invaders_back,1024u)==0xc2b6b4d0u);
    assert(sizeof(invaders_intro)==1024u && crc32(invaders_intro,1024u)==0x5c55e996u);
}
static void test_intro_and_render_pure(void) {
    invaders_game_t g, before;
    uint8_t buffer[1024];
    Invaders_CoreInit(&g,123u);
    assert(g.phase==INVADERS_TITLE && g.lives==3u && g.monster_count==24u);
    Invaders_CoreRender(&g,buffer);
    assert(memcmp(buffer,invaders_intro,1024u)==0);
    Invaders_CoreStart(&g);
    assert(g.phase==INVADERS_READY);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.phase==INVADERS_PLAYING);
    memcpy(&before,&g,sizeof(g));
    Invaders_CoreRender(&g,buffer);
    Invaders_CoreRender(&g,buffer);
    assert(memcmp(&before,&g,sizeof(g))==0);
}
static void test_ship_fire_and_formations(void) {
    invaders_game_t g;
    Invaders_CoreInit(&g,1u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    for(unsigned i=0;i<150u;i++) {
        Invaders_CoreTick(&g,INVADERS_MOVE_LEFT,1u);
        assert(g.ship_pos>=5u && g.ship_pos<=114u);
        assert(g.shot_y>=-1 && g.shot_y<=6);
        assert(g.enemy_shot_half_y<=16u);
        if(g.phase!=INVADERS_PLAYING)break;
    }
    assert(g.ship_pos<=56u);
    Invaders_CoreInit(&g,1u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    for(unsigned i=0;i<15u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.formation_x>20u);
    assert(g.monster_count==24u);
}
static void test_enemy_hit(void) {
    invaders_game_t g;
    Invaders_CoreInit(&g,4u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    /* The canonical enemy at (row=0,col=2) is stationary for this tick. */
    g.formation_x=20u;g.formation_y=0u;g.formation_frame=0u;g.formation_frame_max=255u;
    g.shot_x=20u+2u*14u+6u;
    g.shot_y=0;g.shot_half=1;
    Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.monsters[0][2]==8 && g.monster_count==23u);
    Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.monsters[0][2]==9 && g.monster_count==23u);
    for(unsigned i=0;i<3u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.monsters[0][2]==-1);
}
static void test_shield_two_way(void) {
    invaders_game_t g;
    Invaders_CoreInit(&g,5u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    g.shot_y=6;g.shot_x=19u;g.shot_half=1u;
    Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u); /* shot moves page 6, shield hit */
    assert(!(g.shields[0]&0x80u) && g.shot_y==-1);
    g.enemy_shot_x=20u;g.enemy_shot_half_y=12u;
    Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(!(g.shields[0]&0x40u) && g.enemy_shot_half_y==16u);
}
static void test_ufo_bonus_and_levels(void) {
    invaders_game_t g;
    Invaders_CoreInit(&g,6u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    g.lives=2u;g.ufo_x=120;g.shot_x=122u;g.shot_y=0;g.shot_half=1;
    Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    assert(g.lives==3u && g.ufo_x==-120);
    for(unsigned stage=0;stage<12;stage++) {
        for(unsigned y=0;y<4u;y++)for(unsigned x=0;x<6u;x++)g.monsters[y][x]=-1;
        g.monster_count=0u;
        Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        assert(g.phase==INVADERS_LEVEL_CLEAR);
        for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        assert(g.phase==INVADERS_READY && g.level==(stage<9u?stage+1u:9u));
        for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        assert(g.phase==INVADERS_PLAYING);
    }
}
static void test_death_lives(void) {
    invaders_game_t g;
    Invaders_CoreInit(&g,7u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
    for(unsigned death=0;death<3u;death++) {
        g.enemy_shot_x=g.ship_pos+5u;
        g.enemy_shot_half_y=14u;
        Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        assert(g.phase==INVADERS_DYING);
        for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        if(death<2u) {
            assert(g.lives==2u-death && g.phase==INVADERS_READY);
            for(unsigned i=0;i<30u;i++) Invaders_CoreTick(&g,INVADERS_NO_MOVE,0u);
        } else assert(g.lives==0u && g.phase==INVADERS_GAME_OVER);
    }
    Invaders_CoreStart(&g);
    assert(g.lives==3u && g.phase==INVADERS_READY && g.level==0u);
}
static void test_stress(void) {
    invaders_game_t g;
    uint8_t output[1024];
    Invaders_CoreInit(&g,11u);Invaders_CoreStart(&g);
    for(unsigned i=0;i<3000u;i++) {
        if(g.phase==INVADERS_GAME_OVER) Invaders_CoreStart(&g);
        Invaders_CoreTick(&g,(invaders_move_t)(i%3u),(uint8_t)((i%7u)==0u));
        if(i%3u==0) Invaders_CoreRender(&g,output);
        assert(g.level<=9u && g.lives<=3u && g.monster_count<=24u);
        assert(g.shot_y>=-1 && g.shot_y<=6);
        assert(g.ufo_x>=-120 && g.ufo_x<=127);
    }
}
int main(void) {
    test_assets();test_intro_and_render_pure();test_ship_fire_and_formations();
    test_enemy_hit();test_shield_two_way();test_ufo_bonus_and_levels();
    test_death_lives();test_stress();
    puts("Tiny Invaders core: original CRCs, pure render, volleys, shields, levels, UFO, lives PASS");
    return 0;
}
