# Firmware/APP · 应用层

业务逻辑所在地，不直接操作寄存器。按职责分两类：

| 目录 | 放什么 |
|---|---|
| `framework/` | 系统启动流程、时间基准/Uptime、事件或任务调度、低功耗状态机、看门狗、日志 |
| `algorithm/` | 协议解析、滤波与标定、状态估计、参数换算等纯计算模块 |

规则：

- 只调用 `BSP/`、`Peripheral/` 暴露的接口，不包含 HAL 头文件之外的外设细节；**`algorithm/` 尽量不依赖 HAL，保证可主机侧单测**。
- 新增模块 `xxx.c/.h` 后必须在 Keil 工程里加入 `Application/APP` 分组。
- 业务状态机与硬件时序分开：时序在 `Peripheral/`，策略在 `APP/`。


### PB10A low-power framework

The PB02B/IH01A cooperative PM state machine limits depth to `PM_SLEEP_DEPTH_DEEPSLEEP` (STM32G070 STOP1, not Standby). Automatic idle timeout defaults to 60s; `pm_api_set_sleep_timeout(0)` disables it. Five keypad EXTI lines can wake STOP1. The registered MCU device saves and disables ADC internal VREFEN and TSEN, suspends SysTick, enters STOP1, restores the generated clock tree and ADC paths, then resumes SysTick. No external peripheral power hooks are present; board-level STOP current and wake reliability are untested.


### Devices, RTC and scanning

SW6306 (PB02B-derived, 5 mΩ battery shunt for PB10A), SC7A20 (PB02B-derived) and RDA5807 (Collar-Radio-derived) are integrated under BSP with a blocking HAL I2C1 adapter. They are not auto-started. The I2C Scanner WORD page probes one address per UI tick; ENTER rescans, UP/DOWN changes result page and BACK exits. The Collar-Radio style Time page offers aligned integer date/time edits via HAL RTC. RTC uses backup-register DR0 validity marking so CubeMX-generated default time/date is applied only on a fresh backup domain. RTC fields refresh every second outside active parameter editing; RTC currently uses LSI and is not crystal-accurate. STOP1 and all hardware peripherals remain untested on board.


### Application versus service separation

`APP/functions/` contains installable-style user applications such as the I2C scanner, while `APP/services/` contains shared system services such as the RTC time editor. The application-layer 1 ms clock now uses the independent `BSP/Time/millis.c` counter, incremented from the SysTick IRQ user region. coroOS, PM, UI keys, OLED timeout, scanning and BSP device-driver delays read this clock, independently of HAL's `uwTick`. `Millis_Delay()` requires enabled SysTick interrupts and must not execute during STOP1.


## Architecture (2026-11)

- `APP/app_task.c` registers coroOS tasks and polls shared services
- `Scheduler/coroOS.*` is the stackless cooperative scheduler inspired by Adam Dunkels' Protothreads (not FreeRTOS, no priorities)
- `APP/services/*_algo.c` holds reusable beep, RTC and system algorithms; `time_service.c` retains the MiaoUI calendar editor
- `APP/functions/` hosts independent user-facing MiaoUI applications; Powerbank and Radio have direct callback entry points, but do not auto-activate power/FM peripherals yet
- `APP/i18n.c/h` defines a shared localization interface and placeholder English-only catalog
- `BSP/BMP180` ports Collar-Radio compensation code with Cortex-M0+ safe endian calibration; `BSP/MMC5603` supports register I/O, product ID and low-power configuration
- `BSP/I2C` has one I2C1 HAL handle, IRQ-driven transfers and nested transaction-level locking. The legacy synchronous API waits in the foreground for each interrupt completion, so during those waits other coroOS threads **do not run**. This must not be confused with true nonblocking coroutine I2C; migrating the sensor drivers to yielding protothreads is future work
- In the I2C transaction layer, `I2C_TransactionBegin/End` must bracket any multi-frame register sequence. A driver must not yield while holding that lock. When a transfer times out and abort cannot complete, further I2C operations fail closed until reset

### Seven icon entries

Powerbank (app), Radio (app), Settings (TEXT: Display/Time/System), Statistics (TEXT: Power/Battery/Sensors), Game (ICON), Tools (ICON), About (WORD). Empty/statistics/game items are placeholders and not hardware telemetry.

### Temporary three-stage board test (2026-11)

`APP/functions/hw_test.h` currently sets `PB10A_HW_TEST_AUTORUN=1`.
On each boot, after the normal CubeMX peripheral initialization and the
one-shot `beep_init()`, `Time_Init()`, `SC7A20_Init()`, it runs:

1. **USB-A**: PA6 SUB_EN high for 8 s; the screen displays raw 12-bit
   PA0/ADC_CH0 and PA5/ADC_CH5 samples from the existing 4-channel ADC-DMA
   configuration. Connect only a small known-safe load; use a meter at USB-A
   to establish actual 5 V. These raw ADC counts alone do not constitute a
   calibrated pass. PA6 is reset low after the window.
2. **Buzzer**: PB1/TIM3_CH4, PSC=63, ARR=499 -> 2 kHz / 50% duty,
   for 500 ms. Listen for audible output; firmware cannot detect sound.
3. **Radio**: start RDA5807, initial tune at 87.0 MHz, then seek upward
   once. Leave audio **hardware-muted**, regardless of volume register:
   PB10A testing found even VOL=0 may be uncomfortably loud. After the
   timer expires the normal MiaoUI menu appears; enter Radio and press
   ENTER only when headphone level is safe to deliberately toggle DMUTE.

After completing board checks, set `PB10A_HW_TEST_AUTORUN=0` to disable
all three automatic tests without touching the regular application.

The seven home tiles now use Collar-Radio 30x30 XBM assets appropriate to
their categories. Powerbank has a custom 30x30 battery/bolt XBM icon;
the Radio page includes the Spleen-based 24px font from Collar-Radio.
The original shared 12px fusion-pixel menu font remains in place.

`SC7A20_Init()` runs once in `APP_TaskInit()`; its PB02B 25 Hz, ±2 g,
high-resolution BDU setup is unchanged. The PB10A sensor interrupt is PB5,
not PB02B's PB8 shared interrupt. The experimental temperature-ADC
compatibility register is not yet board-verified.
