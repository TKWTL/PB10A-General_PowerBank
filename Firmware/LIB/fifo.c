#include "fifo.h"

/* On ARMv6-M (Cortex-M0+), the compiler's generic __atomic builtins can
 * call __atomic_load_4 / __atomic_store_4, adding a runtime dependency.
 * Use naturally aligned volatile 32-bit reads/writes and ARM DMB instead:
 * each endpoint has one writer, and such loads/stores are indivisible.
 * On non-ARM hosts use actual compiler atomics for concurrency tests. */
#if defined(__arm__) || defined(__thumb__)
#define FIFO_LOAD_ACQUIRE(p)   fifo_arm_load(p)
#define FIFO_LOAD_RELAXED(p)   (*(p))
#define FIFO_STORE_RELEASE(p,v) fifo_arm_store((p),(v))
#define FIFO_STORE_RELAXED(p,v) (*(p)=(v))
static uint32_t fifo_arm_load(const volatile uint32_t *p) {
    uint32_t v = *p;
    __asm__ volatile ("dmb" ::: "memory");
    return v;
}
static void fifo_arm_store(volatile uint32_t *p, uint32_t v) {
    __asm__ volatile ("dmb" ::: "memory");
    *p = v;
}
#elif defined(__GNUC__) || defined(__clang__)
#define FIFO_LOAD_ACQUIRE(p)    __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define FIFO_LOAD_RELAXED(p)    __atomic_load_n((p), __ATOMIC_RELAXED)
#define FIFO_STORE_RELEASE(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
#define FIFO_STORE_RELAXED(p,v) __atomic_store_n((p),(v),__ATOMIC_RELAXED)
#else
#error "fifo.c requires ArmClang or GCC/Clang for release/acquire barriers"
#endif

uint8_t FIFO_Init(fifo_t *fifo, uint8_t *storage, uint32_t capacity) {
    if (fifo == 0 || storage == 0 || capacity < 2u ||
        capacity > (1ul << 30) || (capacity & (capacity - 1u)) != 0u)
        return 0u;
    fifo->storage = storage;
    fifo->capacity = capacity;
    fifo->mask = capacity - 1u;
    FIFO_STORE_RELAXED(&fifo->head, 0u);
    FIFO_STORE_RELAXED(&fifo->tail, 0u);
    return 1u;
}

void FIFO_Clear(fifo_t *fifo) {
    if (fifo == 0) return;
    FIFO_STORE_RELAXED(&fifo->head, 0u);
    FIFO_STORE_RELAXED(&fifo->tail, 0u);
}

uint32_t FIFO_Count(const fifo_t *fifo) {
    uint32_t head, tail, used;
    if (fifo == 0 || fifo->storage == 0 || fifo->capacity == 0u) return 0u;
    /* Weak snapshot, safe as a hint even if the opposite endpoint moves. */
    head = FIFO_LOAD_ACQUIRE(&fifo->head);
    tail = FIFO_LOAD_ACQUIRE(&fifo->tail);
    used = head - tail;
    return used > fifo->capacity ? 0u : used;
}

uint32_t FIFO_Free(const fifo_t *fifo) {
    if (fifo == 0 || fifo->storage == 0) return 0u;
    return fifo->capacity - FIFO_Count(fifo);
}

uint8_t FIFO_Push(fifo_t *fifo, uint8_t value) {
    uint32_t head, tail;
    if (fifo == 0 || fifo->storage == 0 || fifo->capacity == 0u) return 0u;
    head = FIFO_LOAD_RELAXED(&fifo->head);
    tail = FIFO_LOAD_ACQUIRE(&fifo->tail);
    if (head - tail >= fifo->capacity) return 0u;
    fifo->storage[head & fifo->mask] = value;
    FIFO_STORE_RELEASE(&fifo->head, head + 1u);
    return 1u;
}

uint8_t FIFO_Pop(fifo_t *fifo, uint8_t *out) {
    uint32_t head, tail;
    if (fifo == 0 || out == 0 || fifo->storage == 0 || fifo->capacity == 0u)
        return 0u;
    tail = FIFO_LOAD_RELAXED(&fifo->tail);
    head = FIFO_LOAD_ACQUIRE(&fifo->head);
    if (head == tail) return 0u;
    *out = fifo->storage[tail & fifo->mask];
    FIFO_STORE_RELEASE(&fifo->tail, tail + 1u);
    return 1u;
}

uint32_t FIFO_Write(fifo_t *fifo, const uint8_t *src, uint32_t length) {
    uint32_t i;
    if (src == 0) return 0u;
    for (i = 0u; i < length && FIFO_Push(fifo, src[i]); ++i) {}
    return i;
}

uint32_t FIFO_Read(fifo_t *fifo, uint8_t *dst, uint32_t length) {
    uint32_t i;
    if (dst == 0) return 0u;
    for (i = 0u; i < length && FIFO_Pop(fifo, &dst[i]); ++i) {}
    return i;
}
