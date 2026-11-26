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
  with CRC into the Keil `.bss.noinit` SRAM area; this retains calibration
  across MCU reset while SRAM has power, **not** across total power loss.
  Short CONF starts sampling from the introductory page. Arrow and side-view
  icons show the six poses, with an inverted active pose, completed checks
  and a stability bar. Success returns after 1.2 s. Both applications release
  their PM lock on leave.

- `compass_app.c/.h`: magnetic heading with gravity-based tilt compensation,
  vector EMA, rotating compass rose with Fusion N/E/S/W glyphs, eight full
  direction names, and magnetic strength.
  The displayed right edge (native SC7A20 -X) is forward; actual R2
  display rotation adds 180 degrees. `COMPASS_MAG_*_AXIS` defaults to
  Y,-X,Z based on the user's inferred mounting; verify against real north.
  The compass subtracts the retained native-axis hard-iron offsets before
  mapping and filtering. No gain matrix or declination correction. Both sensors use the 40 ms tool
  target. Short or long CONF exits. Invalid/stale samples, rapid acceleration,
  near-vertical forward axis or weak horizontal field suppress heading.
- `compass_cal_app.c/.h`: guided six-pose magnetic offset calibration.
  First rest on the large face (Z-), then turn it upside down (Z+).
  The remaining four poses use screen-up (Y-) north/east/south/west.
  The arrow always points screen-right. Under R2 the requested heading
  shifts by 180 degrees so the native edge retains its required heading.
  Short CONF captures 16 new samples; LEFT redoes the previous pose.
  Flat entry/hold tolerances are 8/10 degrees and 100 mG span, versus
  5 degrees and 50 mG for the four screen-up poses. The pose comparison
uses the original atan2/Q15 acceptance boundaries without square root or
angle evaluation. Short stale gaps
  preserve progress, and three fresh bad flat poses abort the capture.
  Any key exits after success. The six-face storage order and existing
  bias formula are preserved. The bias record uses sixteen bytes of .bss.noinit;
  Statistics remains raw. The leave hook restores sampling/PM.
- `altimeter_app.c/.h`: pressure, temperature and altitude from the raw
  BMP180 sample. LEFT/RIGHT cycle Calib. Height, Relative Height and
  Standard Height, starting with calibrated height. UP/DOWN open the
  known-elevation editor or zero relative height. In the editor,
  UP/DOWN adjust metres (1/10/100 m with repeat), RIGHT starts averaging
  16 fresh samples, and LEFT cancels. CONF exits the tool and discards
  edits or samples in progress. An inverse lookup and the pressure ratio
  determine the sea-level reference using 32-bit integer arithmetic.
  The 12-byte CRC32-protected record in .bss.noinit survives powered SRAM resets.
  Relative zero remains session-only. Altimeter and Accel Calibration use
  12 px text with translation keys and leave the display corners clear.

Shared foundation services such as RTC calendar editing and the buzzer
are provided by `APP/framework/`; device logic resides in `APP/algorithm/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
