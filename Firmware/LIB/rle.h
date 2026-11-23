/* PB10A four-mode byte RLE decoder. Pure C99, no heap, HAL or u8g2. */
#ifndef PB10A_LIB_RLE_H
#define PB10A_LIB_RLE_H

#include <stdint.h>

typedef enum {
    RLE_OK = 0,
    RLE_ERR_ARGUMENT = -1,
    RLE_ERR_INPUT = -2,
    RLE_ERR_OUTPUT = -3
} rle_status_t;

typedef enum {
    RLE_WRITE = 0,
    RLE_XOR = 1
} rle_operation_t;

/* Decode the entire src into exactly dst_size bytes.
 * Header: high 2 bits select literal / zero / 0xff / repeated byte;
 *         low 6 bits give run length minus one (1..64 bytes).
 * Literal consumes N additional bytes; repeat consumes one byte.
 * RLE_XOR applies each decoded byte with dst[i] ^= value.
 * No overlapping source/destination, no dynamic allocations.
 * On malformed input the destination may be partially modified.
 * Intended for offline-compressed, known-size image assets.
 */
rle_status_t RLE_Decode(const uint8_t *src, uint32_t src_size,
                        uint8_t *dst, uint32_t dst_size,
                        rle_operation_t operation);

#endif
