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
  and long hold exits. See [README](games/invaders/README.md).

- `games/bomber/`: GPL-3.0-or-later Tiny Bomber port by Daniel C.
  Original landscape 128x64 bitmaps, five characters, one bomb, fixed cross
  blast and three 14-byte destructible maps. Four random-wall-turn enemies,
  three lives and accelerated enemies after all three maps. Native OLED DMA,
  CONF short-release bomb / hold-to-pause / long-hold exit, normal auto STOP1.
  See [documentation](games/bomber/README.md).

- `games/pacman/`: GPL-3.0-or-later TinyPacman port based on Daniel C's
  Tinyjoypad source. Original page-packed 128x64 artwork, pixel collision
  and four random-turn ghosts; 22ms logic step, 44ms MiaoUI render request.
  Turn the board 90 degrees counterclockwise to view portrait maze.
  Hold CONF to pause, long-hold to exit. All mutable session state stays in
  RAM. Automatic STOP1 is allowed. See [game documentation](games/pacman/README.md).

- `games/arkanoid/`: Tiny Arkanoid by Daniel Champagne, with five original
  layouts and portrait controls. See [game documentation](games/arkanoid/README.md).

- `games/tetris/`: Tetris v3, with the original 12×19 well, seven
  pieces and graphics. See [game documentation](games/tetris/README.md).

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
  its `img_leveler` and `img_leveler_cal` 30x30 XBM icons live in this
  module (derived from Collar-Radio).
  Calibration uses six stable ±axis placements, then records zero/slope
  with CRC into the Keil `.noinit` SRAM area; this retains calibration
  across MCU reset while SRAM has power, **not** across total power loss.
  Both applications release their PM lock on leave.

- `compass_app.c/.h`: magnetic heading with gravity-based tilt compensation,
  vector EMA, rotating compass rose, eight directions and magnetic strength.
  The displayed right edge (native SC7A20 -X) is forward; actual R2
  display rotation adds 180 degrees. `COMPASS_MAG_*_AXIS` defaults to
  Y,-X,Z based on the user's inferred mounting; verify against real north.
  The compass subtracts the retained native-axis hard-iron offsets before
  mapping and filtering. No gain matrix or declination correction. Both sensors use the 40 ms tool
  target; only long CONF exits. Invalid/stale samples, rapid acceleration,
  near-vertical forward axis or weak horizontal field suppress heading.
- `compass_cal_app.c/.h`: guided six-pose magnetic offset calibration.
  With the screen facing up (native SC7A20 Y-), aim its right edge N/E/S/W
  in order. Then rest on the large face (Z-), aim right north, and flip
  around the screen-left/right axis (Z+) without changing heading. Each
  short CONF captures 16 new magnetic samples at the 40 ms target, within
  5 degrees of the expected gravity axis. Stale data or a 50 mG per-axis
  span aborts that capture; LEFT redoes the previous pose; long CONF exits.
  The last two poses reuse the Leveler horizon, with progress at the bottom.
  The bias is (M5+M6-MN+MS)/2, with a nondegenerate N/S and E/W check;
  three native-axis uint16_t offsets (32768 = zero mG), CRC16 and tag use
  twelve bytes of .noinit. Statistics remains raw; reset retention requires
  powered SRAM. Calibration locks R0; the leave hook restores sampling/PM.
- `altimeter_app.c/.h`: Pa snapshot, integer standard-atmosphere LUT,
  standard/relative height in 0.1 m, pressure, sensor temperature and
  vertical speed from the closest sample at least 5 s old. Short CONF
  toggles STD/REL; LEFT or RIGHT zeros the relative height and selects REL;
  long CONF exits. Relative reference is session RAM only.
  Both sensor tools use the Leveler PM block/leave hook pattern and normal
  screen auto-rotation; no new thread or I2C owner.

Shared foundation services such as RTC calendar editing and the buzzer
are provided by `APP/framework/`; device logic resides in `APP/algorithm/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
