/* PB10A byte FIFO: fixed power-of-two capacity, SPSC only. Pure C99. */
#ifndef PB10A_LIB_FIFO_H
#define PB10A_LIB_FIFO_H

#include <stdint.h>

typedef struct {
    uint8_t *storage;
    uint32_t mask;
    uint32_t capacity;
    volatile uint32_t head; /* Written only by the producer. */
    volatile uint32_t tail; /* Written only by the consumer. */
} fifo_t;

/* Capacity is a power of 2 in [2, 2^30]. All storage is caller-owned.
 * Concurrent use: exactly one producer and one consumer (e.g. ISR/main).
 * On ARM, aligned 32-bit volatile access plus DMB avoids libatomic and IRQ
 * masking; host compiler atomics provide acquire/release ordering.
 * Producer must not also read/pop; consumer must not also write/push.
 * FIFO_Init and FIFO_Clear require both endpoints to be stopped.
 */
uint8_t FIFO_Init(fifo_t *fifo, uint8_t *storage, uint32_t capacity);
void FIFO_Clear(fifo_t *fifo);
uint32_t FIFO_Count(const fifo_t *fifo);
uint32_t FIFO_Free(const fifo_t *fifo);
uint8_t FIFO_Push(fifo_t *fifo, uint8_t value); /* 0 if full */
uint8_t FIFO_Pop(fifo_t *fifo, uint8_t *out);   /* 0 if empty */
uint32_t FIFO_Write(fifo_t *fifo, const uint8_t *src, uint32_t length);
uint32_t FIFO_Read(fifo_t *fifo, uint8_t *dst, uint32_t length);

#endif
