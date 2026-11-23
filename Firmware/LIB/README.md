# Hardware-independent firmware algorithms

`Firmware/LIB/` holds small, pure C utilities without STM32 HAL, MiaoUI,
U8g2 or application dependencies. Keeping algorithms here **does not** imply
that game images are already converted to compressed form. Add an algorithm's
`.c` file to the Keil project only when it is used; with `-Os` and LTO,
measure the final linked Flash rather than assuming abstraction saves space.

## Four-mode byte RLE

`rle.h/.c`: offline encoder, firmware-only decoder. The control byte's
upper two bits select the mode, and its lower six bits encode `N-1` (N=1..64):

| Bits 7..6 | Meaning | Additional input |
|---|---|---|
| `00` | Copy `N` literal bytes | `N` bytes |
| `01` | Output `N` zeros | none |
| `10` | Output `N` `0xFF` bytes | none |
| `11` | Repeat the next byte `N` times | one byte |

`RLE_Decode(src,src_len,dst,uncompressed_len,RLE_WRITE)` reconstructs a
byte-identical image; `RLE_XOR` instead XORs decoded bytes into the destination
(e.g. delta image). Requires non-overlapping buffers and an **exact** expected
uncompressed size. Invalid/truncated streams return an error but may modify the
prefix of the destination. No input/output access out of the provided bounds.
Works for 1024-byte native SSD1306 page images, 120-byte XBM menu icons or
any byte stream. **Not compatible** with U8g2's internal font RLE.

## 32-bit single-producer/single-consumer FIFO

`fifo.h/.c`: caller-owned byte buffer, power-of-two capacity `2..2^30`.
Head/tail are monotonically incrementing 32-bit sequence counters; wrap uses
unsigned arithmetic and indexing uses `counter & (capacity-1)`. The producer
owns head and the consumer owns tail. On Cortex-M0+ use aligned 32-bit volatile access with DMB publication (avoids
libatomic). Host builds use compiler atomics. No extra count field, mutex
or interrupts-disabled critical section. Compatible with ArmClang/GCC.

- Safe for **one producer and one consumer** running in different contexts,
  including a main loop and one ISR, if used as documented. Never invoke push
  from two producers, or pop from two consumers, concurrently.
- `FIFO_Init` and `FIFO_Clear` require producer and consumer to be stopped.
- `FIFO_Count`/`FIFO_Free` are snapshots, not reservations; they may become
  stale immediately if the other endpoint moves.
- Does not manage DMA cache coherency, memory-mapped device buffers or
  multi-core arbitration. On Cortex-M0+ there is no data cache.
- Existing Radio/Beep queues are not migrated automatically: verify RAM,
  timing and binary size benefit first.

Run `bash Firmware/LIB/tests/run.sh` on a host with GCC and Clang installed.
