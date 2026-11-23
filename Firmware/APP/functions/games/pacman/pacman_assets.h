/* TinyPacman by Daniel C (2018-2021), GPL-3.0-or-later.
 * Unmodified page-packed original graphics extracted from spritebank.h.
 * byte index = (y_page * 128 + x), bits 0..7 are successive rows. */
#ifndef PB10A_TINYPACMAN_ASSETS_H
#define PB10A_TINYPACMAN_ASSETS_H
#include <stdint.h>
extern const uint8_t pacman_BackBlitz[1024];
extern const uint8_t pacman_back[1024];
extern const uint8_t pacman_dots[1024];
extern const uint8_t pacman_caracters[192];
extern const uint8_t pacman_fruits[32];
extern const uint8_t pacman_Music[140];
/* Dedicated MiaoUI Game index icon: 30x30 XBM, 120 Flash bytes. */
extern const uint8_t img_pacman[120];
#endif
