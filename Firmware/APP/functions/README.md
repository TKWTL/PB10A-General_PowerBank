# User-contributed applications

`functions/` hosts independently usable MiaoUI applications, analogous to
user-installed apps on an operating system. These modules may use system
services and BSP interfaces, but the services and scheduler must not depend on
them.

- `i2c_scanner.c/.h`: visual UI restored from Collar-Radio: initial
  1.5s title `I2C / Scanner` in the original 24px font, followed by
  the full 16x8 hexadecimal address grid using the original 4px font.
  Probes safe addresses 0x08..0x77, one probe per UI frame (other cells
  show `--`); automatic rescan after 5s. CONF short release rescans
  immediately, long CONF exits. Scanner prevents automatic sleep until
  the page exits; it does not monopolize coroOS.

Shared/system services such as RTC calendar editing remain in `APP/services/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
