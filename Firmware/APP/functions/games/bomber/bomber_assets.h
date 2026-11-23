/* Tiny Bomber by Daniel C (2018-2021). GPL-3.0-or-later.
 * Original byte images are reconstructed losslessly from four-mode RLE.
 * Fixed walls and the destructible block bitmaps remain independent. */
#ifndef PB10A_BOMBER_ASSETS_H
#define PB10A_BOMBER_ASSETS_H
#include <stdint.h>
extern const uint8_t bomber_BackBlitz_rle[257];
extern const uint8_t bomber_back_rle[125];
extern const uint8_t bomber_caracters[192];
extern const uint8_t bomber_fire[216];
extern const uint8_t bomber_BlocDetect[128];
extern const uint8_t bomber_level[42];
extern const uint8_t bomber_bomb[24];
extern const uint8_t bomber_Music[42];
/* Dedicated MiaoUI Game index icon: 30x30 XBM, 120 Flash bytes. */
extern const uint8_t img_bomber[120];
#endif
