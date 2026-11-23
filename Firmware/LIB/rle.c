#include "rle.h"
#include <string.h>

rle_status_t RLE_Decode(const uint8_t *src, uint32_t src_size,
                        uint8_t *dst, uint32_t dst_size,
                        rle_operation_t operation) {
    uint32_t si = 0u, di = 0u;

    if ((src == 0 && src_size != 0u) || (dst == 0 && dst_size != 0u) ||
        (operation != RLE_WRITE && operation != RLE_XOR))
        return RLE_ERR_ARGUMENT;

    while (si < src_size) {
        uint8_t control = src[si++];
        uint32_t count = (uint32_t)(control & 0x3fu) + 1u;
        uint8_t mode = (uint8_t)(control >> 6);
        uint8_t value = 0u;
        uint32_t i;

        /* Check bounds using subtraction to avoid uint32_t overflow. */
        if (count > dst_size - di) return RLE_ERR_OUTPUT;
        if (mode == 0u) {
            if (count > src_size - si) return RLE_ERR_INPUT;
            if (operation == RLE_WRITE)
                memcpy(dst + di, src + si, count);
            else
                for (i = 0u; i < count; ++i) dst[di + i] ^= src[si + i];
            si += count;
        } else {
            if (mode == 3u) {
                if (si == src_size) return RLE_ERR_INPUT;
                value = src[si++];
            } else if (mode == 2u) {
                value = 0xffu;
            }
            if (operation == RLE_WRITE)
                memset(dst + di, value, count);
            else if (value != 0u)
                for (i = 0u; i < count; ++i) dst[di + i] ^= value;
        }
        di += count;
    }
    return di == dst_size ? RLE_OK : RLE_ERR_OUTPUT;
}
