//         >>>>>  T-I-N-Y  T-R-I-S v3 for ATTINY85  GPL v3 <<<<<
//                     Programmer: Daniel C 2019-2021
//              Contact EMAIL: electro_l.i.b@tinyjoypad.com
//                      https://WWW.TINYJOYPAD.COM
//           https://sites.google.com/view/arduino-collection

//  Tiny-tris is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.

//  You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
#ifndef PB10A_TETRIS_ASSETS_H
#define PB10A_TETRIS_ASSETS_H

#include <stdint.h>

/* Each row uses LSB = leftmost column; original 5x5 pivot is (2, 2). */
static const uint8_t tetris_pieces[][5] = {
    {0x00, 0x04, 0x0E, 0x00, 0x00}, /* Piece 0 */
    {0x00, 0x04, 0x0C, 0x04, 0x00},
    {0x00, 0x00, 0x0E, 0x04, 0x00},
    {0x00, 0x04, 0x06, 0x04, 0x00},
    {0x00, 0x00, 0x0C, 0x0C, 0x00}, /* Piece 1 */
    {0x00, 0x00, 0x0C, 0x06, 0x00}, /* Piece 2 */
    {0x00, 0x02, 0x06, 0x04, 0x00},
    {0x00, 0x08, 0x0C, 0x04, 0x00}, /* Piece 3 */
    {0x00, 0x00, 0x06, 0x0C, 0x00},
    {0x04, 0x04, 0x04, 0x04, 0x00}, /* Piece 4 */
    {0x00, 0x00, 0x1E, 0x00, 0x00},
    {0x00, 0x04, 0x04, 0x06, 0x00}, /* Piece 5 */
    {0x00, 0x02, 0x0E, 0x00, 0x00},
    {0x00, 0x0C, 0x04, 0x04, 0x00},
    {0x00, 0x00, 0x0E, 0x08, 0x00},
    {0x00, 0x04, 0x04, 0x0C, 0x00}, /* Piece 6 */
    {0x00, 0x00, 0x0E, 0x02, 0x00},
    {0x00, 0x06, 0x04, 0x04, 0x00},
    {0x00, 0x08, 0x0E, 0x00, 0x00},
};

/* Start and exclusive end of each piece's rotation range. */
static const uint8_t tetris_piece_offsets[] = {0u, 4u, 5u, 7u, 9u, 11u, 15u, 19u};

/* Dedicated MiaoUI Game index icon: 30x30 XBM, 120 Flash bytes. */
extern const uint8_t img_tetris[120];

#endif
