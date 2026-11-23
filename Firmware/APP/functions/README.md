# User-contributed applications

`functions/` hosts independently usable MiaoUI applications, analogous to
user-installed apps on an operating system. These modules may use system
services and BSP interfaces, but the services and scheduler must not depend on
them.

- **Shared game resources:** `games/game_shared.c/.h` provides one
  owner-checked 1024-byte SRAM asset cache used in turn by Pac-Man,
  Bomber and Invaders; see [games/SKILL.md](games/SKILL.md).
- **Game porting guide:** [`games/SKILL.md`](games/SKILL.md) covers source/asset audits,
  nonblocking state machines, OLED frame buffers, gameplay inputs, STOP1,
  third-party licenses and regression tests. Game index icons are defined
  by each game's `*_assets.c/.h`, not duplicated in MiaoUI's image library.

- `games/invaders/`: GPL-3.0-or-later Tiny Invaders v3.1 by Daniel C.
  Original horizontal 128x64 images, 4x6 formation, 10 layouts, shields,
  UFO and one-projectile held-UP shooting. Nonblocking 33ms game logic,
  original page rendering in existing OLED DMA buffer, normal auto STOP1.
  LEFT/RIGHT move; hold UP to fire; CONF short release starts, hold pauses
  and long hold exits. See [README](games/invaders/README.md) and
  [host tests](games/invaders/tests/).

- `games/bomber/`: GPL-3.0-or-later Tiny Bomber port by Daniel C.
  Original landscape 128x64 bitmaps, five characters, one bomb, fixed cross
  blast and three 14-byte destructible maps. Four random-wall-turn enemies,
  three lives and accelerated enemies after all three maps. Native OLED DMA,
  CONF short-release bomb / hold-to-pause / long-hold exit, normal auto STOP1.
  See [documentation](games/bomber/README.md) and [tests](games/bomber/tests/).

- `games/pacman/`: GPL-3.0-or-later TinyPacman port based on Daniel C's
  Tinyjoypad source. Original page-packed 128x64 artwork, pixel collision
  and four random-turn ghosts; 22ms logic step, 44ms MiaoUI render request.
  Turn the board 90 degrees counterclockwise to view portrait maze.
  Hold CONF to pause, long-hold to exit. All mutable session state stays in
  RAM. Automatic STOP1 is allowed. See [game documentation](games/pacman/README.md)
  and [tests](games/pacman/tests/).

- `games/breakout/`: 8x5 Breakout with nine preset maps and random maps from
  stage 10. The existing MIT license remains alongside its own sources.

- `i2c_scanner.c/.h`: visual UI restored from Collar-Radio: initial
  1.5s title `I2C / Scanner` in the original 24px font, followed by
  the full 16x8 hexadecimal address grid using the original 4px font.
  Draws `--` or the responding hex address **one cell at a time**
  across the original 128-position grid; only 0x08..0x77 are probed.
  Unscanned cells remain blank, and automatic rescan starts 5s after
  completion. CONF short release rescans immediately; long CONF
  **or any directional key** exits. The scanner prevents automatic
  sleep while open without monopolizing coroOS.

- `tools_motion_app.c/.h`: PB02B six-face SC7A20 calibration and full-screen
  Leveler port. Both are MiaoUI WORD apps under Tools and use the
  SC7A20 algorithm's sequential RAM sample mirror, never UI-owned I2C.
  Leveler draws a 128x64 gravity-translated/rotated horizon with a
  plumb double-circle view and a default-font tilt angle at bottom right;
  its icons are the Collar-Radio Gradienter and Calibration 30x30 XBM.
  Calibration uses six stable ±axis placements, then records zero/slope
  with CRC into the Keil `.noinit` SRAM area; this retains calibration
  across MCU reset while SRAM has power, **not** across total power loss.
  Both applications release their distinct PM/rotation lock on leave.

Shared foundation services such as RTC calendar editing and the buzzer
are provided by `APP/framework/`; device logic resides in `APP/algorithm/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
