/* Original Tiny Invaders v3.1 images and stage tables, Daniel C GPLv3+. */
#ifndef PB10A_INVADERS_ASSETS_H
#define PB10A_INVADERS_ASSETS_H
#include <stdint.h>

extern const int8_t invaders_levels[240]; /* upstream MonstersLevels */
extern const uint8_t invaders_lives[15]; /* upstream LIVE */
extern const uint8_t invaders_shoot[2]; /* upstream SHOOT */
extern const uint8_t invaders_monsters[168]; /* upstream Monsters */
extern const uint8_t invaders_ship[26]; /* upstream vesso */
extern const uint8_t invaders_back[1024]; /* upstream back */
extern const uint8_t invaders_intro[1024]; /* upstream intro */
/* Dedicated MiaoUI Game index icon: 30x30 XBM, 120 Flash bytes. */
extern const uint8_t img_invaders[120];
#endif
