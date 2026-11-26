#ifndef PB10A_MINI_CRC_H
#define PB10A_MINI_CRC_H

#include <stdint.h>

/* CRC16-CCITT-FALSE: polynomial 0x1021, initial value 0xffff. */
uint16_t crc16_ccitt(const void *data, unsigned size);
/* Three consecutive 32-bit words in the STM32's little-endian byte order. */
uint16_t crc16_ccitt_words3(uint32_t a, uint32_t b, uint32_t c);

#endif
