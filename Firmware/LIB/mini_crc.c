#include "mini_crc.h"

__attribute__((noinline)) uint16_t crc16_ccitt(const void *data, unsigned size) {
    const uint8_t *bytes = data;
    uint16_t crc = 0xffffu;
    while (size--) {
        crc ^= (uint16_t)*bytes++ << 8;
        for (unsigned bit = 0u; bit < 8u; ++bit)
            crc = (uint16_t)((crc & 0x8000u) ? (crc << 1) ^ 0x1021u : crc << 1);
    }
    return crc;
}

__attribute__((noinline)) uint16_t crc16_ccitt_words3(uint32_t a, uint32_t b, uint32_t c) {
    const uint32_t words[] = {a, b, c};
    return crc16_ccitt(words, sizeof(words));
}
