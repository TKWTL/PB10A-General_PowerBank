# Tiny Invaders v3.1 — PB10A port

Source: **Daniel C**, Tinyjoypad ATTINY85 / Tiny Invaders v3.1 (2018–2021),
**GPL-3.0-or-later**. Original source is supplied in the Tiny-invaders ZIP.
The original resource data is preserved losslessly: the full-screen
`intro[]` and `back[]` are now four-mode RLE byte streams (decoded
CRC32s unchanged), while the other original five tables retain identical
uncompressed byte sequences. There are still ten `MonstersLevels[]` layouts. The full upstream `COPYING.txt` is retained as
`LICENSE-GPL-3.0`. No Arduino, AVR or `FastTinyDriver` code is required.

## Gameplay

- Landscape 128×64 SSD1306 bitmap, unchanged artwork; use the existing
  1024-byte U8g2 DMA framebuffer without a second full-screen buffer.
- Maximum 24 monsters in 4×6 formation, 14-pixel spacing; whole formation
  advances in 2-pixel steps and descends/changes direction at each boundary.
  Movement interval decreases with remaining active monsters.
- One active player shot; hold physical UP to fire again when the projectile
  disappears and the original two-tick reload delay completes.
- One enemy projectile; its source is chosen randomly from the original
  three upper rows, and projectile cadence increases with stage number.
- Three destructible 16-pixel-wide shelters tracked by exactly six bytes;
  the original shape masks and both friendly/enemy hit behavior are preserved.
- Occasional original UFO fly-by, +1 life on hit up to the 3-life cap.
- Ten original 24-entry formation layouts; level index 9 is repeated after
  clearing the final layout; initial life count **3 total chances** rather
  than the upstream 3 plus one accidental extra.

Original `Tiny_Flip()` and `MyShoot()` both advanced the simulation during
rendering. In this port **`Invaders_CoreTick()` is the only function that
changes game state**, including projectile movement, impacts and shelter bits;
`Invaders_CoreRender()` is read-only. Original delays/busy-wait sound loops
are replaced by a 33ms initial logic step and 44ms UI render request. Real
hardware tuning may be needed to recover the original subjective pacing.

## PB10A controls and lifecycle

| Physical key | Action |
| --- | --- |
| LEFT / RIGHT | Slide spaceship horizontally |
| UP held | Repeated fire, one projectile in flight |
| DOWN | Reserved |
| CONF short release on title/over | Start / restart |
| CONF held | Pause the game (no logic steps) |
| CONF long hold | Return to game menu (MiaoUI BACK) |

Game state is static RAM only. Leaving the page releases the shared
backdrop cache, not the simulation state; re-entry rebuilds the backdrop
and resets the elapsed-time baseline. MCU reset discards the session.
No peripheral-specific coroutine, persistent
writes, dynamic allocation, PM sleep lock or artificial idle refresh is used.
After a STOP1 gap over 150ms the accumulated simulation time is discarded;
projectiles and movement do not run at accelerated speed after wake.

## Files

- `invaders_core.c/.h`: pure portable simulation + read-only page rendering.
- `invaders_assets.c/.h`: original byte-exact fontless bitmap and level arrays.
- `game_invaders.c/.h`: MiaoUI WORD page, native keys, nonblocking Beep Service,
  direct OLED DMA buffer, restart and leave hook.

## Game-index icon

The user-provided `img_invaders[120]` XBM icon is defined in
`invaders_assets.c` and declared in `invaders_assets.h`. It occupies
120 bytes of Flash, is row-major/LSB-first (unlike the original
SSD1306 page-packed sprites), and is used directly by
`APP/MiaoUI/ui_conf.c`. The original gameplay artwork is
unmodified. See [shared porting guide](../SKILL.md).
 
## Shared resource cache and RLE

`invaders_back_rle[420]` decodes at game entry into GameBuffer's sole
1024-byte SRAM asset block. The core reads it by native SSD1306
page and scrolling X offset during active play, with no per-pixel RLE
cost. The original full-screen `invaders_intro_rle[557]` decompresses
directly into the OLED framebuffer on title/game-over screens. Neither
operation allocates a second display buffer. The title image is handled
by `Invaders_CoreRender()`; `Invaders_CorePixel()` composes active-scene
pixels only. Exit unbinds the cached backdrop and clears owner data,
but the game state is retained and the backdrop is restored on re-entry.
STOP1 preserves SRAM and discards simulation backlog.
