/* TinyPacman by Daniel C (2018-2021), GPL-3.0-or-later.
 * Page-packed maze and collision masks restored byte-exact by RLE/XOR;
 * 64 compact dot records preserve all original pixel masks and order. */
#ifndef PB10A_TINYPACMAN_ASSETS_H
#define PB10A_TINYPACMAN_ASSETS_H
#include <stdint.h>
extern const uint8_t pacman_BackBlitz_rle[362];
extern const uint8_t pacman_back_xor_rle[207];
extern const uint16_t pacman_dot_records[64]; /* x7 | page3 << 7 | mask3 << 10 */
extern const uint8_t pacman_dot_masks[8];
extern const uint8_t pacman_caracters[192];
extern const uint8_t pacman_fruits[32];
extern const uint8_t pacman_Music[140];
/* Dedicated MiaoUI Game index icon: 30x30 XBM, 120 Flash bytes. */
extern const uint8_t img_pacman[120];
#endif
