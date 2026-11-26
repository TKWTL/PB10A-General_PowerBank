#include "mini_crc.h"

#if MINI_CRC_USE_HW
#include "stm32g0xx_ll_crc.h"

__attribute__((noinline)) uint32_t crc32_words3(uint32_t a, uint32_t b, uint32_t c) {
    LL_CRC_ResetCRCCalculationUnit(CRC);
    LL_CRC_FeedData32(CRC, a);
    LL_CRC_FeedData32(CRC, b);
    LL_CRC_FeedData32(CRC, c);
    return LL_CRC_ReadData32(CRC);
}
#else
/* Keep the same word-wise checksum for hardware-independent callers. */
static uint32_t crc32_update(uint32_t crc, uint32_t word) {
    crc ^= word;
    for (unsigned bit = 0u; bit < 32u; ++bit)
        crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    return crc;
}

uint32_t crc32_words3(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t crc = crc32_update(0xffffffffu, a);
    crc = crc32_update(crc, b);
    return crc32_update(crc, c);
}
#endif
