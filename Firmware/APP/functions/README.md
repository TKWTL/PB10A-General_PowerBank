# User-contributed applications

`functions/` hosts independently usable MiaoUI applications, analogous to
user-installed apps on an operating system. These modules may use system
services and BSP interfaces, but the services and scheduler must not depend on
them.

- `i2c_scanner.c/.h`: nonblocking I2C1 scanner. One bus probe per 20 ms
  UI pass; ENTER restarts, UP/DOWN selects result pages, BACK exits.
  The scan blocks automatic power-down while open.

Shared/system services such as RTC calendar editing remain in `APP/services/`.
For new MiaoUI applications, register their callbacks/pages in `ui_conf.c`
and their sources in the Keil project. Do not put them in CubeMX-managed
folders.
