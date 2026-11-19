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

### Board bring-up and ADC telemetry (2026-11)

Basic bench bring-up has been performed for the buzzer, FM radio and ADC.
The temporary boot-time hardware test, including forced FM auto-seek, has
been removed. Startup now always runs the standard MiaoUI/coroOS
scheduler; Radio starts only when its menu application is entered.

The independent USB-A remains enabled at PA6. The ADC service is
`APP/services/power_adc_algo.c`, driven only by the fifth coroOS thread:
one software-triggered ADC1 four-rank scan (PA0, PA5, MCU temperature,
VREFINT), DMA completion via IRQ, then `THRD_DELAY(500)` before repeating.
Sampling remains active in RUN and UI_OFF and is paused around STOP1
through the PM device hook. PA6 is unaffected by these hooks.

The current CubeMX-generated configuration uses ADC1 HSI clock DIV1,
39.5-cycle sampling for external channels and 79.5-cycle sampling for
internal channels; four ranks, software triggering, normal DMA1 Channel2,
16-bit transfers, memory increment, and DMA Channel2/3 IRQ enabled.
Keep `.ioc` as the configuration source of truth; do not maintain a
private patch to the vendor HAL files.

Statistics -> Power reports:
- `ADC Err`: 0 = success; 1 = calibration failed; 2 = start failed;
  3 = 50 ms timeout; 4 = ADC/DMA error callback; 5 = pending.
- `ADC HAL`: `HAL_ADC_GetError()`; `DMA HAL`: `HAL_DMA_GetError()`.
  These can show zero after an earlier state-machine failure because
  `HAL_ADC_Start_DMA()` resets the ADC error code.

The reproduced G0 calibration failure (`hadc1.State=0x13`,
`ErrorCode=0x01`, second `ADC_DISABLE_TIMEOUT` branch) and the verified
CubeMX clock adjustment are documented in
[`Docs/Debug/2026-11-19_stm32g0_adc_calibration.md`](../../Docs/Debug/2026-11-19_stm32g0_adc_calibration.md).

### Key input (Collar-Radio-style page mapping)

Keys are active-low and debounced for 25 ms. On ICON pages physical
LEFT/RIGHT navigate previous/next and UP/DOWN emit MINUS/PLUS.
On TEXT pages UP/DOWN navigate previous/next and LEFT/RIGHT emit
MINUS/PLUS. The data widget uses MINUS/PLUS to decrement/increment
editable integers (and scaled-integer float parameters). CONF short
press/release is ENTER, while holding CONF for at least 700 ms is BACK;
BACK is required for exiting WORD applications and parameter widgets.

`Vout~ mV` uses an assumed 3.3V analog reference and the netlist's
R39=680k / R41=220k divider; confirm voltage with a multimeter.

The seven home tiles now use Collar-Radio 30x30 XBM assets appropriate to
their categories. Powerbank has a custom 30x30 battery/bolt XBM icon;
the Radio page restores the original Collar-Radio 12px/24px fonts,
status XBM icons, FM dial, discovered-station markers and RTC footer.
The original shared 12px fusion-pixel menu font remains in place.

`SC7A20_Init()` runs once in `APP_TaskInit()`; its PB02B 25 Hz, ±2 g,
high-resolution BDU setup is unchanged. The PB10A sensor interrupt is PB5,
not PB02B's PB8 shared interrupt. The experimental temperature-ADC
compatibility register is not yet board-verified.

### Fatal exception snapshot and reset

`APP/services/fault_record.c` defines `g_fault_record` in the 256-byte
`.noinit` SRAM region reserved by
`MDK-ARM/Ld/PB10A-General_PowerBank.sct`. This also gives the previously
empty `RW_NOINIT` region an actual input section.

NMI, HardFault, CubeMX `Error_Handler()`, and weak startup default IRQ
handlers invoke `FaultRecord_Reset()` instead of remaining in their
infinite loops. It disables maskable interrupts, saves the minimum snapshot
and requests `NVIC_SystemReset()`. It deliberately does *not* invoke HAL
drivers, display or serial logging, or program Flash in a fault context.

Inspect the public `g_fault_record` symbol in the Keil debugger after a
software reset. The layout is defined in `fault_record.h`:
`magic`, `version`, `reason`, `exception` (IPSR),
`flash_eccr`, `syscfg_cfgr2`, `rcc_csr`, and `checksum`.
Call `FaultRecord_IsValid()` before treating the record as genuine.
Only the latest fault is retained; it can be overwritten by a repeated
reset loop. SRAM retention is not guaranteed across power loss, boot
loader/debugger memory initialization or a separate SRAM reset.
In severely corrupted Flash/SRAM/stack conditions even this minimal
recorder may fail before it can request reset.

**G070/G071 ECC/parity caution:** Flash ECC (64-bit data + 8 ECC bits)
is automatic and does not need enabling. `FLASH_ECCR_ECCD` signals an
uncorrectable double error and raises NMI. The SRAM facility is *parity
detection*, not correcting ECC; set the active-low
`RAM_PARITY_CHECK` option bit to **0** to enable. Both parts change
from 36KB usable SRAM to 32KB when parity is enabled. **Do not enable
that option byte with this repository's current 36KB scatter file or
uninitialized .noinit retention policy.** First redesign the scatter
regions (e.g. move last-256B .noinit from 0x20008F00 to 0x20007F00),
update startup SRAM parity initialization, and decide how to preserve
and validate previous fault records without reading uninitialized
parity bits. No option bytes have been changed by this commit.
