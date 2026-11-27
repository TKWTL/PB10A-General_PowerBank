# Hardware-independent firmware algorithms

`Firmware/LIB/` holds small utilities without STM32 HAL, MiaoUI,
U8g2 or application dependencies. The CRC utility optionally uses the
STM32G0 LL CRC peripheral. Pac-Man, Bomber and Invaders now use
this RLE codec for native page data, and Beep Service uses FIFO records. Add an algorithm's
`.c` file to the Keil project only when it is used; with `-Oz` and LTO,
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
- Public element APIs are `FIFO_Push1/Pop1`, `Push2/Pop2`, and
  `Push4/Pop4`, taking 1, 2 or 4 bytes per call with all-or-nothing
  commit. Multi-byte values use little-endian byte order and may wrap
  across the end of the caller-owned buffer. `FIFO_Write/Read` remain
  byte-stream helpers for UART-like transfers (may transfer only a prefix).
- Beep Service enqueues a single 32-bit sequence pointer via
  `FIFO_Push4` and reads it via `FIFO_Pop4`. PB10A Cortex-M0+ pointers
  are 4 bytes; eight-entry capacity remains. Alarm loop and fault tones
  are arbitrated by Beep Service outside the FIFO; the FIFO has no priority
  or audio semantics.
  `Push1/Pop1` are suitable for IH01A-style USART software byte rings,
  although IRQ-DMA UART integration itself belongs in its hardware driver.
- Existing Radio queues are not migrated automatically.

## Small integer/string formatter

`mini_format.c/.h` is reused from IH01A's BSP/Console formatter.
`mini_snprintf`, `mini_vsnprintf` and `mini_vformat` support only
`%% %c %s %d %i %u %x %X`, flags `-`/`0`, field width and a
single `l` length modifier. The parser intentionally does **not**
support `%f`, `%zu` or `%.Ns`. PB10A's protocol label is copied
directly with destination-size truncation rather than parsing `%.15s`.
No `mini_stdio.c` and no override of libc's `printf`.

## Game resource cache integration

`Firmware/APP/functions/games/game_shared.c/.h` provides an independent
owner-checked 1024-byte SRAM cache, in addition to SSD1306's existing
1024-byte framebuffer. The game adapters call `RLE_Decode` at entry
and render; the core logic reads the uncompressed cache for random-access
collision or scrolling. Pac-Man applies an RLE_XOR delta after the
base bitmap decode. Invalid source/destination sizes are checked;
buffers must not alias. No dynamic allocation.

`pressure_altitude_dm()` interpolates 81 integer-metre standard-atmosphere
nodes (30–110 kPa in 1 kPa steps, 162 bytes), returning decimetres.
The standard reference is 101325 Pa; no floating-point runtime is used.
`altitude_pressure_pa()` provides the inverse interpolation over the same
node table. The application computes a calibrated sea-level pressure from
known elevation and uses a pressure ratio for calibrated altitude; the
library itself remains standard-atmosphere only. Both functions return 0
outside their table domains.
Offline evaluation at all 80,001 integer Pa inputs found a maximum
interpolation/rounding error of 0.993 m against the stated model.

## Shared calibration utilities

`mini_crc.c/.h` computes CRC-32/MPEG-2 over three MSB-first 32-bit words.
On STM32G070, `MINI_CRC_USE_HW` defaults to 1 and reuses the default
CubeMX CRC32 configuration initialized before the application. Hardware mode is
foreground-only; the fault snapshot uses its own checksum rather than
calling this utility from an exception. Other
targets can use the equivalent software path. Retained records store
the full CRC32 and a separate version marker; old CRC16 records are
not compatible.
`mini_math.c/.h` shares the three-axis accumulation, span check and mean
for the magnetometer and accelerometer, while callers retain their own
pose, sampling and retry policies.
