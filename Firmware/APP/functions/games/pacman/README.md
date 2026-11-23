# TinyPacman PB10A v2

Original **Tiny PACMAN** (Daniel C, 2018–2021), licensed under GPL-3.0-or-later.
The byte-for-byte original `BackBlitz`, `back`, `dots`, `caracters`,
`fruits` and `Music` tables were extracted from the supplied `spritebank.h`.
Gameplay is rewritten in portable C while preserving the original movement,
6x7 pixel-mask wall checks, random turns, pellet order and power-pill timing.
This is **not** the previous MIT Ardu-man/21x11 game.

The native SSD1306 128x64 physical page buffer is used unscaled. Turn the
entire PB10A **counterclockwise 90 degrees** to read the portrait maze. In this
orientation, the PB10A physical key mapping (confirmed against user feedback)
is UP=PACMAN_UP, DOWN=PACMAN_DOWN, LEFT=PACMAN_LEFT and
RIGHT=PACMAN_RIGHT. LEFT/RIGHT here are actual physical buttons rather
than names of the Pac-Man movement enum; their mappings were swapped
after an initial on-device report showed both directions reversed.

On the attract/READY screen, the otherwise unused lives/fruit strip shows
`TURN 90 CCW` in a 5x7 pixel font, oriented for reading after turning the
board. The text is painted into the existing 1024-byte SSD1306 page buffer;
no additional display buffer or u8g2 rotation is required. During gameplay
the original lives/fruit drawings are unchanged.

Directional controls support **one queued turn**. A fresh directional key
press replaces the previous pending direction; releasing a key does not
cancel it. While the original pixel collision mask still blocks that turn,
Pac-Man continues straight and executes it at the first legal opportunity.
Reversing direction on the current axis is immediate. CONF pressed or short
released clears the queued turn without stopping current motion. Input is
captured on debounced key transitions rather than from MiaoUI auto-repeat;
existing physical UP/DOWN/LEFT/RIGHT mappings are unchanged. Pending input is
reset on death, next level, restart and leaving/re-entering the game.

CONF short release starts/restarts when waiting/game-over. During play,
**holding** CONF freezes updates; releasing resumes without any menu-toggle
pause. Holding CONF for ~700 ms raises MiaoUI BACK and leaves the WORD page.
No `delay()`, dedicated coroutine, Arduino, heap, EEPROM/Flash save, PM sleep
block, or `pm_api_refresh_idle()` calls. Automatic STOP1 is left to the normal
power manager. Gaps >150 ms discard simulation backlog (including power-pill
countdowns). The first frame following STOP1 does not fast-forward.

Original timing: ~22 ms logic tick and ~44 ms render request. Stage speed
starts at 200, falls by 10 to minimum 10, bonus lives at 160/120/80/40/10;
life counter is corrected to exactly 3 chances. Win and death cues run via
short, optional, low-priority Beep Service sequences rather than AVR busy
waits. The original full `Music` byte sequence remains preserved in assets,
but is **not** played to avoid prolonged exclusive buzzer use.

## Files

- `game_pacman.c/.h` MiaoUI adapter, fixed physical rotation, keys and DMA.
- `pacman_core.c/.h` original-style state, ghosts, frame steps, collisions.
- `pacman_assets.c/.h` original unchanged graphics/audio table bytes.
- `LICENSE-GPL-3.0` full upstream license (GPL-3.0-or-later).

Host tests check array bounds, original asset hashes/sizes, collision and
lifecycle. Keil compilation and STOP1 device validation remain to be done.

## Game-index icon

The user-provided `img_pacman[120]` XBM icon is defined in
`pacman_assets.c` and declared in `pacman_assets.h`. It occupies
120 bytes of Flash, is row-major/LSB-first (unlike the original
SSD1306 page-packed sprites), and is used directly by
`APP/MiaoUI/ui_conf.c`. The original gameplay artwork is
unmodified. See [shared porting guide](../SKILL.md).
