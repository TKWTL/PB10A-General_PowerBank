---
name: pb10a-game-porting
description: Port and maintain Tinyjoypad/AVR and other retro games in PB10A STM32G070/MiaoUI without losing original art, gameplay semantics or STOP1 compatibility.
---

# PB10A game porting — implementation skill

Scope: `Firmware/APP/functions/games/`. First inspect each complete original
archive (`.ino`, graphics header, display driver, input, sound, screenshots,
license and README). The original game is the baseline; differentiate source
bugs, actual PB10A hardware mismatches and intentional gameplay changes.

## Directory standard

```text
games/
  SKILL.md
  game_shared.c/.h       One 1024-byte owner-checked SRAM asset cache
  <name>/
    game_<name>.c/.h       MiaoUI adapter: controls, frame buffer, beeps
    <name>_core.c/.h       Portable C game state, mechanics and collisions
    <name>_assets.c/.h     Original const data and its own 30x30 index icon
    README.md              Controls, behavior, deviations, tests
    LICENSE-...            Source license, original author attribution
    tests/                 Host core/adapter regression tests and stubs
```

Do not create a separate generic game engine, thread, full-size image buffer,
malloc allocator or EEPROM/Flash save merely to standardize code. Reuse the
existing Beep Service and MiaoUI display/keyboard interfaces.

## Source extraction and licensing

1. Identify upstream release/version and exact license, retain original
   author attribution and license text, and update
   `Firmware/THIRD_PARTY_NOTICES.md` for new third-party game imports.
2. Inventory image dimensions, byte lengths, native bit layout, level masks,
   animation-frame boundaries and audio. Save CRC32 fingerprints for
   original resource arrays. Keep original resource bytes unchanged unless
   user explicitly requests different graphics or gameplay.
3. Replace blocking Arduino/AVR `delay()`, `_delay_ms()`, `PROGMEM`,
   direct Tinyjoypad OLED I2C transfers and software-delay tone loops with
   PB10A's native APIs and `const` flash tables.

## Two bitmap formats (important)

**Gameplay**: original Tinyjoypad SSD1306 art can be page-packed:
`index = page * 128 + x`, eight *vertical* pixels per byte. Its full
128x64 framebuffer is 1024 bytes. Use `Disp_GetBufferPtr()` and
`Disp_SendBuffer()` without allocating another 1024-byte buffer.

**Game menu**: MiaoUI `UI_PAGE_ICON` calls
`Disp_DrawXBMP(x, y, 30, 30, logo)`. XBM is *row-major* and
least-significant-bit-first. Every 30px scanline is 4 bytes; a 30x30
icon consumes exactly 120 bytes, and the top two bits of each fourth byte
must be zero. Do not pass a page-packed sprite as an XBM logo.

Store supplied icon data as `const uint8_t img_<name>[120]` in that
game's `<name>_assets.c`, declare `extern` in `<name>_assets.h`,
include the assets header in `APP/MiaoUI/ui_conf.c`, and bind
`AddItem(...,img_<name>,...)`. Do not duplicate icons in global
`APP/MiaoUI/images/image.c`, or add extra Keil compilation units.
For the owner-supplied Pac-Man icon, the final all-zero 31st scanline
is not part of the 30px UI and may be dropped; all visible data stays intact.

## Porting mechanics and timing

1. Replace original blocking `loop()` and `goto` states with an explicit
   READY / PLAYING / DYING / CLEAR / OVER state machine in a portable C
   core. Keep the changing game state in a static object (RAM only).
2. Advance all motion, collisions, shot/bomb fuses, shield destruction,
   stage and life state in **one fixed-step core tick**. Do not mutate
   gameplay in render callbacks: in original Invaders, for example,
   `Tiny_Flip()` and `MyShoot()` also update bullets/shields.
3. Use `Millis()` and `UI_RequestNextFrame()`; calibrate each game's
   step size separately (Pac-Man and Bomber roughly 22ms logic, Invaders
   roughly 33ms; render request roughly 44ms). No busy-wait sounds.
4. Physical key states come from `KEY_GetState()` as debounced holds,
   not the menu auto-repeat. Always verify actual PCB orientation: the
   enum direction and human-facing direction may differ, especially
   for portrait Pac-Man (rotate the board 90 degrees CCW).
5. CONF short **release** is `UI_ACTION_ENTER`, hold pauses, long
   BACK (~700ms) exits; `UI_SetLeaveHook()` cleans up. Invaders uses
   held UP to repeat fire, LEFT/RIGHT to move, and leaves CONF for
   pause/start/exit. Bomber places a bomb on short CONF release.
   For buffered turns, remember only the latest new key press, retain
   it until a legal turn, and clear it with CONF.
6. Never call `pm_api_set_sleep_block()` or `pm_api_refresh_idle()`
   for a game. On STOP1 wake or a long update gap (current threshold
   roughly 150ms), discard elapsed simulation backlog rather than
   accelerating enemy motion, bullets, animations or bomb timers.
7. Short audio cues use nonblocking `beep_enqueue()` without
   preempting active system tones.

## Collision and sprite consistency lessons

- **Bomber:** original `BlocDetect[16]==0xFE` is an exceptional
  renderer sentinel. Drawing maps it to column 1 but collision must
  skip both `0xFE` and `0xFF`; treating FE as a solid adjacent
  block caused the spawn lane at x=9 to jam at y=48. Fixed walls
  and the 14-byte destructible obstacle map must remain separate.
  PB10A deliberately changed the original 24x24 explosion to a
  40x40 cross: keep animation envelope, damage hitboxes and block
  clearing radius two tiles in sync, including edge clipping.
- **Pac-Man:** preserve native pixel-wall masks, turning only when
  collision allows. Last pressed direction is buffered until a
  legal intersection or later key/CONF; reverse is immediate.
- **Invaders:** preserve 4x6 formation, 10 original level tables,
  original shield-bit state, UFO bonus and one player projectile.
  Keep rendering pure so DMA frame count cannot change hit results.
- **Breakout:** retain its separate MIT upstream license and game
  structure; do not force unrelated games into the same mechanics.

## Check before committing

1. Test each source asset against the original CRC; ensure menu-icon
   array is 120 bytes and row padding bits are zero.
2. Test movement, map edges, sentinel values, collisions, stage
   transitions, damaged pixels, life counts and new bug regressions.
3. Test adapter key holds, CONF short release, hold/long BACK,
   leaving/re-entering, STOP1 time gap and direct DMA buffer use.
4. Run GCC/Clang host builds with C11 `-Wall -Wextra -Werror -Wpedantic`
   and sanitizers as available; host tests do not prove hardware.
5. Register the adapter in `APP/functions/apps.h`, its item/icon/leave
   hook in `APP/MiaoUI/ui_conf.c`, and exactly the necessary .c files
   in `MDK-ARM/PB10A-General_PowerBank.uvprojx`.
6. On device, recheck actual button directions, visuals, damage-vs-art,
   sound, exit and STOP1 resume. Compare Keil .map `Code + RO + RW`
   before/after under `-Os` + LTO: source reuse does not guarantee
   smaller linked Flash.

## Shared 1 KiB cache, byte RLE and sparse dots (PB10A implementation)

- The only shared game resource SRAM is `game_shared.c`'s **1024-byte**
  `s_game_buffer` (additional to the existing SSD1306 DMA framebuffer).
  `GameBuffer_Acquire(GAME_OWNER_*)` obtains exclusive ownership; another
  owner is rejected, the same owner may reacquire without clearing.
  `GameBuffer_Release(owner)` zeros all 1024 bytes and releases the owner.
  Never release on STOP1/display suspension; release only on the UI leave
  hook, and unbind game-core pointers **before** clearing. Breakout needs no
  cache. No heap, lazy allocation or third 1024-byte screen buffer.
- Image compression uses hardware-independent `Firmware/LIB/rle.c`:
  upper two control bits choose literal/zero/FF/repeat, lower six bits
  encode length-1 (1..64). `RLE_WRITE` reconstructs, `RLE_XOR` applies
  a delta. Compress off-device and verify **exact original CRC32** after
  decoding. This is unrelated to u8g2 font RLE.
- **Pac-Man:** `BackBlitz` compressed (362 B) is decoded directly into
  OLED framebuffer for each render; at first entry decode the same maze
  into GameBuffer, then apply the 207-byte `back XOR delta` into it.
  Collision then reads cached SRAM at native page+X offsets. The
  original 1024-byte `dots` raster is replaced by 64 ordered 16-bit
  records + eight original byte masks (136 B). Record bits: X[6:0],
  page[9:7], mask-index[12:10], reserved[15:13]=0. Iterate only the
  64 records for consumption and overlay visible masks directly into
  the OLED buffer. Preserve original page-major/X-increasing order:
  indices 0,1,12,13,50,51,62,63 are the four corner **two-byte power
  pills**; each top corner uses mask 0x06, each bottom corner 0xC0.
  Keep their flashing and ghost-reversal timing; do not merge two
  adjacent entries into one item.
- **Bomber:** decompress the 125-byte static-wall mask to GameBuffer
  at game entry, bind it to the core; decompress 257-byte backdrop
  directly into display buffer each frame. Destructible block bits
  remain distinct from fixed walls; preserve 0xFE collision sentinel
  semantics and the intentional 40x40 blast changes.
- **Invaders:** decompress 420-byte scrolling backdrop to GameBuffer
  at entry, allowing unmodified random X+offset access during play.
  Decode the 557-byte title raster directly to OLED buffer for READY
  and GAME_OVER; normal scene composition is otherwise unchanged.
- Cache is bound by game adapter to a pure C game core; the core does
  not call MiaoUI, RLE or GameBuffer to allocate anything. The game
  adapter performs resource decompression only on first entry, and
  continues to preserve existing long STOP1-gap timing behavior.
- Host tests must reconstruct every original 1024-byte image and
  Pac-Man's 64-dot bitmap and compare historical CRC32s; they must
  additionally verify ownership exclusivity, owner-safe release,
  1024-byte clearing, leave/re-entry and no STOP1 release.
- Capacity accounting: three former Pac-Man, two Bomber and two
  Invaders 1024-byte raster arrays total **7168 bytes** in Flash,
  now encoded in **2064 bytes** total including Pac-Man dot records.
  Gross static data reduction is **5104 bytes**, before decoder,
  pointers and alignment. Measure real `Program Size` after Keil
  `-Os`+LTO; cache adds 1024 bytes to ZI RAM.
