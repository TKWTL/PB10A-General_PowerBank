# User-contributed applications

`functions/` hosts independently usable MiaoUI applications, analogous to
user-installed apps on an operating system. These modules may use system
services and BSP interfaces, but the services and scheduler must not depend on
them.

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

Shared/system services such as RTC calendar editing remain in `APP/services/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
