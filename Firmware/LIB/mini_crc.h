#ifndef PB10A_MINI_CRC_H
#define PB10A_MINI_CRC_H

#include <stdint.h>

/* G070 uses CubeMX's default CRC configuration. Other targets may opt in
 * when they provide the same LL driver and initialized CRC peripheral. */
#ifndef MINI_CRC_USE_HW
#if defined(STM32G070xx)
#define MINI_CRC_USE_HW 1
#else
#define MINI_CRC_USE_HW 0
#endif
#endif

/* CRC-32/MPEG-2: poly 0x04c11db7, init 0xffffffff, no reflection or xorout.
 * Each word is fed MSB-first. Shorter retained payloads pad with zero words.
 * Hardware mode is foreground-only and requires MX_CRC_Init() first. */
uint32_t crc32_words3(uint32_t a, uint32_t b, uint32_t c);

#endif
