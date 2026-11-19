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

Shared/system services such as RTC calendar editing remain in `APP/services/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
