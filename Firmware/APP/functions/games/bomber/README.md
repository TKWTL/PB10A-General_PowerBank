# Tiny Bomber — PB10A MiaoUI port

Source: Daniel C, *Tiny Bomber* for Tinyjoypad ATTINY85 (2018–2021),
GPL-3.0-or-later. The original author name, contact, upstream source and full
COPYING.txt license are retained. This is a source port, not a newly designed
Bomberman clone. The original AVR `FastTinyDriver` and Arduino libraries are
not included or required by PB10A.

## Features and original mechanics

- Native horizontal 128×64 monochrome display; original `BackBlitz`, `back`,
  `caracters`, `fire`, `BlocDetect`, `level`, `bomb`, and `Music` asset bytes
  are preserved without changing or rescaling the artwork.
- One player, four enemies sharing the original random-wall-turn movement;
  no pursuit AI, score, collectible, or more than one simultaneous bomb.
- Three maps cycle as 14-byte MSB-first bitmaps (15×7 = 105 valid cells);
  destruction does **not** modify the fixed collision `back[]` image.
- Bomb fuse is advanced only at Frame 0/8/16, explosion at Frame % 4 == 0,
  and expanded 40×40 cross-blast damage (9 destructible cells) is applied once at
  animation stage 1. Four enemies killed advances the level at Frame 12;
  after completing all three maps, enemies move every logic tick.
- Starts with exactly three lives. Restarting after death keeps destroyed
  blocks and enemies already killed in the *same* level, matching the
  source's `RESTARTLEVEL` behavior, but fixes its extra-life off-by-one.
- Original 6×7 actor/bitmap collision probes; **PB10A custom 40×40 cross**
  extends each arm to two obstacle cells in every direction. Rendering repeats
  the original 8px arm pieces without any new bitmap arrays; damage and block
  clearing use the same reach. Fixed walls still cannot be destroyed.
  The original 24×24 cross is deliberately superseded by this change.
- Original map bounds safety is retained, with
  signed/bounded accesses for map edges and valid cell bits 0..104.
  In collision probes, special `BlocDetect` values `0xFF` **and** `0xFE`
  are ignored as in upstream; only block rendering maps `0xFE` to column 1.
  This preserves the empty leftmost spawn corridor from y=56 to y=40 despite
  the adjacent row-4 obstacle in column 1.

## PB10A controls

| Physical key | Behavior |
| --- | --- |
| UP / DOWN / LEFT / RIGHT | Move in native landscape orientation |
| CONF short release on title / game-over | Start / restart |
| CONF press and hold during play | Freeze game and bomb timer |
| CONF short release during play | Place one bomb if no bomb exists |
| CONF long hold (~700 ms) | MiaoUI BACK / leave game |

No `pm_api_set_sleep_block`, `pm_api_refresh_idle`, additional coroutine,
EEPROM/Flash saves or blocking AVR `Sound()` calls. `Game_BomberRun` consumes
ordinary direction and short-CONF actions but leaves `UI_ACTION_BACK` for
MiaoUI, which invokes `Game_BomberOnLeave`.

Logic runs in nominal 22 ms steps; render requests use 44 ms. A time gap over
150 ms (e.g., STOP1) is discarded, not caught up, so the fuse does not
silently complete during auto sleep. OLED output is written directly into
`Disp_GetBufferPtr()` page bytes, followed by `Disp_SendBuffer()`; the standard
MiaoUI DMA-busy handling owns concurrency. No second 1024-byte RAM buffer.
Short nonblocking audio cues use existing Beep Service when available; the
original 42-byte `Music[]` is kept as an archival source asset but full intro
music and blocking note delays are not played.

## Organization

- `game_bomber.c/.h`: MiaoUI, debounced key holds, CONF, native OLED buffer,
  optional Beep Service and clock-gap handling.
- `bomber_core.c/.h`: actor movement, level transitions, fuse, enemy and
  player collision, packed destructible map and nonblocking state machine.
- `bomber_assets.c/.h`: eight original byte-for-byte arrays in const Flash.
- `LICENSE-GPL-3.0`: unchanged original `COPYING.txt`.
- `tests/`: GCC/Clang host tests, CRC32 asset fingerprints, runtime
  collision, life and frame handling, including ASan/UBSan runs.

Device display and ARMClang integration still require physical verification.

## Game-index icon

The user-provided `img_bomber[120]` XBM icon is defined in
`bomber_assets.c` and declared in `bomber_assets.h`. It occupies
120 bytes of Flash, is row-major/LSB-first (unlike the original
SSD1306 page-packed sprites), and is used directly by
`APP/MiaoUI/ui_conf.c`. The original gameplay artwork is
unmodified. See [shared porting guide](../SKILL.md).
