# Firmware/APP · 应用层

业务逻辑所在地，不直接操作寄存器。按职责分两类：

| 目录 | 放什么 |
|---|---|
| `framework/` | 系统级基础服务：`beep_service`（TIM3 PWM + 队列）、`time_service`（RTC 时间与日历编辑）、低功耗框架 |
| `algorithm/` | 设备服务与算法：ADC/功率、FM、传感器、系统配置、故障记录以及标定与滤波 |

规则：

- 通过 `BSP/` 设备驱动与 `Core/` HAL 适配访问硬件；`algorithm/` 允许在设备服务边界调用驱动和 HAL，但应把可以独立测试的计算尽量与硬件交互隔离。
- 新增模块 `xxx.c/.h` 后必须在 Keil 工程里加入 `Application/APP` 分组。
- 基础系统服务集中在 `framework/`，可复用的设备算法与状态机集中在 `algorithm/`；不再使用空的 `Peripheral/` 和 `APP/services/` 目录。


### PB10A low-power framework

The PB02B/IH01A PM controller supports cooperative device callbacks:
`PM_HOOK_DONE`, `PM_HOOK_PENDING`, `PM_HOOK_ERROR`. STOP1 is entered
only after every suspend operation finishes; errors cancel sleep and resume
devices. Default idle timeout is 60 s, `pm_api_set_sleep_timeout(0)`
disables it, and keypad EXTI can wake STOP1. External nodes reside in
`APP/algorithm/power_algo.c`, `sc7a20_algo.c`, `mmc5603_algo.c`, `bmp180_algo.c`, `radio_algo.c`;
OLED and STM32 platform nodes reside in `pm_device_builtin.c`.
Power sequencing, callback order and hardware caveats are recorded in
[`Docs/Debug/2026-11-19_pm_callbacks.md`](../../Docs/Debug/2026-11-19_pm_callbacks.md).
Board-level current and wake reliability still require measurement.


### Devices, RTC and scanning

SW6306 (PB02B-derived, 5 mΩ battery shunt for PB10A), SC7A20 (PB02B-derived) and RDA5807 (Collar-Radio-derived) use the LL I2C1 adapter retained in `Core/Src/i2c.c` / `Core/Inc/i2c.h` USER CODE sections. SW6306 now initializes and is refreshed by an independent 100ms coroOS service thread. The I2C Scanner WORD page has regained the original Collar-Radio title and 16x8 address matrix. The Collar-Radio style Time page offers aligned integer date/time edits via HAL RTC. RTC uses backup-register DR0 validity marking so CubeMX-generated default time/date is applied only on a fresh backup domain. RTC fields refresh every 500 ms outside active parameter editing; RTC uses LSE in the current CubeMX configuration. STOP1 and all hardware peripherals remain untested on board.


### Application versus service separation

`APP/functions/` contains user-facing MiaoUI apps such as the I2C scanner. `APP/framework/time_service.c` owns RTC timekeeping and calendar editing; `APP/framework/beep_service.c` owns the PWM buzzer backend and asynchronous tone queue. Device algorithms, PM hooks and fault logging reside in `APP/algorithm/`. The application-layer 1 ms clock now uses the independent `BSP/Time/millis.c` counter, incremented from the SysTick IRQ user region. coroOS, PM, UI keys, OLED timeout, scanning and BSP device-driver delays read this clock, independently of HAL's `uwTick`. `Millis_Delay()` requires enabled SysTick interrupts and must not execute during STOP1.


## Architecture (2026-11)

- `APP/app_task.c` registers coroOS tasks and polls shared services
- `Scheduler/coroOS.*` now wraps the actual Protothreads 1.4 (`Scheduler/pt1.4/pt.h`, `pt-sem.h`, `lc-switch.h`), as in Handheld-Fan; ten separate cooperative tasks are registered (not FreeRTOS, no priorities)
- `APP/framework/beep_service.c` consolidates the former beep hardware PWM and nonblocking melody sequencer; `APP/framework/time_service.c` consolidates RTC calendar/uptime calculations with the MiaoUI time editor. Remaining device logic, fault recorder and system algorithms reside in `APP/algorithm/`
- `APP/functions/` hosts independent user-facing MiaoUI applications. Powerbank displays cached SW6306 voltage, current, percentage and protocol; Radio is forced low-power at boot and starts only when its WORD page is entered (or kept running in explicit background ON mode)
- `APP/i18n.c/h` defines a shared localization interface and placeholder English-only catalog
- `BSP/BMP180` ports Collar-Radio compensation code with Cortex-M0+ safe endian calibration; `BSP/MMC5603` supports register I/O, product ID and low-power configuration
- `Core/Src/i2c.c` USER CODE owns the LL I2C1 master state machine and IRQ completion, including new `ASYNC_I2C_Transmit/Receive`, asynchronous scanner probe and bounded abort handling. Old `BSP/I2C/bsp_i2c.c/.h` were removed and Keil references updated. The sensor and radio application paths use IRQ-completed register transactions; legacy synchronous compatibility functions remain in the BSP drivers. **SW6306 now enables `SW6306_USE_PROTOTHREAD`** and uses the IRQ-backed asynchronous transfer API from its own exclusive service coroutine. Other modules must not directly run another SW6306 protothread concurrently.
- In the I2C transaction layer, synchronous `I2C_TransactionBegin/End` brackets non-yielding legacy transactions. Asynchronous operations use an independent single-in-flight admission check and persistent completion flags; on timeout, a stalled abort poisons the HAL adapter until reset. Do not use stack buffers for asynchronous transfers

### Home menu

Powerbank, Radio, Game, Tools, Settings and Statistics appear in that order. Statistics contains Power, Battery, Sensors and System. Open About from Statistics → System. After reset the firmware first opens the Powerbank dashboard. Short or long CONF opens the home menu from the dashboard.

Settings lists PowerLimit, Ports, Protocol#1 and Protocol#2 before Display.
They expose 32 RAM-only settings and actions derived from PB02B.

- PowerLimit: output 5–105 W, input 5–100 W, both in 5 W steps.
- Ports: C1 input/output and A1 output fast-charge switches, BUS Idle
  Timeout (Off by default), Idle Threshold (0–100 mA, 4 mA steps, default 12),
  Aux-A Enable (on by default), and Aux Idle Current (10–100 mA, default 50).
  Turning Aux-A Enable off stops the output and rejects new manual and
  insertion requests. Re-enabling it permits a new request.
- Protocol#1: PD input/output, PD yield to SCP/VOOC/UFCS, 11/21 V PPS,
  UFCS input/output and manual PPS/UFCS capability broadcasts.
- Protocol#2: QC, FCP, AFC, SCP, PE, SFCP, VOOC and SVOOC switches.

All SW6306 writes are dispatched by the existing owner coroutine.
Power changes and broadcasts can run on active sessions. Protocol and
fast-charge changes wait for fresh, idle port status and a released
external-load meter path. UI edits are coalesced and requeued after
failed writes. PowerLoad detects loss of the applied power configuration;
reinitialization restores the applied power limits and requeues disabled
protocol/port switches. MCU reset returns settings to defaults.
BUS idle shutdown is disabled while charging, in low-current mode,
during external-load requests or metering, or with invalid telemetry.
The independent Aux-A idle timeout remains 60 seconds.

The dashboard's second row shows power, the third row voltage, and the
fourth row protocol, battery percentage and NTC temperature. The BUS and
Aux-A discharge arrows require at least 100 mW of measured output. The BUS
arrow additionally requires a real port path. RTC time/date are at the bottom.
ETA uses filtered battery power and measured full/remaining energy. It first
appears after 5 seconds of stable mode at 200 mW or more, is recalculated
every 10 seconds, and counts down between estimates. END adds ETA to RTC
time and appends a day offset when needed. The two LED brightness lines
show the effective WLED brightness. The dashboard
requests 5 ms during the slide and 50 ms after it settles. Published readings
stay visible during refresh and wake-up. Failed transfers clear the affected
SW6306 fields until a fresh result arrives.

### Board bring-up and ADC telemetry (2026-11)

Basic bench bring-up has been performed for the buzzer, FM radio and ADC.
The temporary boot-time hardware test, including forced FM auto-seek, has
been removed. Startup now always runs the standard MiaoUI/coroOS
scheduler; Radio starts only when its menu application is entered.

`ADC_RTC_AuxAThread` owns ADC calibration and four-channel LL/DMA scans.
It publishes the mean of 16 scans sampled about 25 ms apart in every
500 ms batch. RTC and system service run at 500 ms independently.
`RTC_IsEditing()` protects pending time-editor changes.

PA6/SUB_EN defaults LOW. Confirmed PA7 insertion requests Aux-A.
In the dashboard, each new DOWN press toggles Aux-A. Once the shared meter
path is ready, SUB_EN goes high. After a 10 ms startup delay, the ADC
owner scans every 5 ms while Aux-A is active and cuts output on any
valid VOUT reading at or below 2 V. Current below 50 mA for 60 continuous
seconds also cuts output. Use `PowerAlgo_AuxA_SetAutoOffCurrent()` to
adjust the threshold. Display readings below 10 mA show as zero.

Active and pending external loads pause automatic sleep. Forced STOP1
turns both outputs off and resolves the shared meter path before ADC
shutdown. Aux-A stays off on ordinary wake and may start after an
insertion wake. A protection shutdown waits for a new request.

### WLED and external load metering

In the Powerbank dashboard, RIGHT raises brightness and LEFT lowers
it. A debounced press changes a level immediately, then repeats every
80 ms while held. A RIGHT press that starts from off is limited to
level 2 until a new press. LEFT reaches zero without a press limit.
TIM1 CH4 drives PA11 at 62.5 kHz (64 MHz timer clock, PSC 0,
ARR 1023). Levels 1–32 use squared compare values capped at 1023
and the existing 160 ms ramp. Level 1 uses compare 1.
The SOS API is available to firmware with no dashboard shortcut.

WLED and Aux-A use fresh SW6306 battery voltage, capacity, NTC and
fault status. They stop on confirmed cutoff or lost protection telemetry.
The temperature lockout trips at 60 °C and clears at 45 °C.
For thermal imaging, WLED_ENABLE_THERMAL_DERATING defaults to 0 in
wled_algo.h. Set it to 1 to restore the PB02B derating table.

The SW6306 owner reuses a live C1/A1 path or requests a shared
synthetic A2 insertion, confirmed by rereading port status. A2 has
no physical connector and its hardware auto-off is disabled in Init.
It is removed after both external loads stop or a real path takes over.
Physical A1 retains its normal auto-off policy. A2 does not trigger
automatic navigation or appear as a dashboard port. ETA pauses during
simultaneous charging and external load.


The current CubeMX-generated configuration uses ADC1 HSI clock DIV1,
39.5-cycle sampling for external channels and 79.5-cycle sampling for
internal channels; four ranks, software triggering, normal DMA1 Channel2,
16-bit transfers, memory increment, and DMA Channel2/3 IRQ enabled.
Keep `.ioc` as the configuration source of truth; do not maintain a
private patch to the vendor HAL files.

Statistics → Power shows BUS V, BUS A, Aux-A V and Aux-A A, each with two decimal places.
BUS readings come from the shared SW6306 bus ADC. Chip Temp °C shows the SW6306 die temperature to one decimal place. Aux-A voltage comes from PA5
via the 680 kΩ / 220 kΩ divider, and Aux-A current comes from PA0 at 0.5 V/A.
Both Aux-A channels use the VDDA measured from VREFINT.

Statistics → Battery shows charge percentage, pack voltage in volts,
full and remaining energy in Wh, and Batt Temp °C from the SW6306 NTC to one
decimal place. The SW6306 service supplies the readings from its ADC, status
and capacity caches. Unavailable readings display as `--`.

Statistics → System shows VDDA in volts to two decimal places, die temperature
to one decimal place and About. Die temperature uses VREFINT-corrected ADC
rank 3 and STM32G0 factory TS_CAL1/TS_CAL2.

The reproduced G0 calibration failure (`hadc1.State=0x13`,
`ErrorCode=0x01`, second `ADC_DISABLE_TIMEOUT` branch) and the verified
CubeMX clock adjustment are documented in
[`Docs/Debug/2026-11-19_stm32g0_adc_calibration.md`](../../Docs/Debug/2026-11-19_stm32g0_adc_calibration.md).

### SW6306 20V input request and integer SHUNT calibration

After raising PISET to 100W, PB10A still requested at most **15V**
through VCHG 0x120. The latest driver permits up to **20V**
(`SW6306_VCHG_MVRSQ_20V`) while retaining battery-voltage-based
priority. Real 100W needs a compatible 20V/5A source, rated cable
and verified battery/thermal margins; setting PISET alone cannot
guarantee it.

The old runtime floating-point battery SHUNT configuration
(`SetBattRShunt(float)` / `GetBattRShunt()`) was removed from
PB10A. Current scale uses four compile-time integers in
`BSP/SW6306/sw6306.h`: BAT numerator/denominator **1/1** and
BUS readout numerator/denominator **1/1**. The previous temporary
2/1 BUS compensation has been removed. Internal SW6306 BUS current
limits are unaffected. The
separate energy calculation still uses float for fractional mWh,
not for a mutable SHUNT coefficient. See the
[SW6306 debug note](../../Docs/Debug/2026-11-19_sw6306_monitor.md).

### Fault history and non-modal notices

Statistics → Fault shows the last observed fault code, its own protection
state (ACTIVE / HISTORY / UNKNOWN) and a saturating lifetime-of-MCU counter.
No Flash log or independent task is used. A failed or stale SW6306 status
read cannot establish recovery.

The existing SW6306 StatusLoad phase consumes E01–E13 from fresh W1C
events and live protection edges. Historical reason bits only help decode
a qualified event; they do not create events by themselves. A successful
W1C acknowledgement re-arms the bit. Failed clears are retried without
counting a persistent event again. Only observed fault and capacity-learning
event bits are acknowledged. N01 (capacity learned) does not increment the
fault count. Events coalesced by the chip cannot be counted separately.

AUX undervoltage/short cutoff reports E20; shared meter startup timeout
reports E40; battery protection uses E01/E04/E41 or the corresponding
SW6306 battery fault code. E50 reports confirmed loss of SW6306 initialization
or repeated initialization failure. Duplicate software notices from the
same shared protection do not increment the fault count. E21 and N02
are reserved: AUX invalid samples currently do not trigger a cutoff, and
there is no low-current-mode operation UI. Protection policy is unchanged.

The priority mailbox overlays INFO/WARNING for 2.5 s or FAULT for 4 s.
ASCII notices reuse the existing fonts; the portrait layout uses the
smaller font. Toast saves at most 456 bytes of affected framebuffer area,
restoring them only after SPI DMA completion, so cached pages and partial
redrawing do not retain the overlay. Mosaic frames are not overlaid.
The timer starts on accepted display submission. Pending notices wait
through UI_OFF/STOP1 without requesting wake or setting a PM sleep block.

### SW6306 staged polling and sleep guard (2026-11-19)

The sixth coroOS thread initializes SW6306 on its first service tick,
then retries at most every five seconds when uninitialized. Its 100ms
staggered phases follow the accessible PB02B reference, not a verified
PB02A source: phase 0 ADC (VBUS/IBUS/VBAT/IBAT), phase 1 Status,
phase 2 Port/CC, phase 3 idle, phase 4 Power/protocol, with Capacity
once per two complete cycles (about one second). The sixth thread is now `PowerAlgo_SW6306Thread`, whose Init and data-load calls yield through Protothreads while the I2C1 transfer is active. Other tasks continue to run.

The Powerbank WORD page displays a RAM snapshot only: shared BUS
mV/mA (not separate per-port current sensing), battery mV/mA,
capacity %, negotiated protocol, and C1/A1 path flags. UI labels
freshness as unavailable until initialization and successful
StatusLoad within the current cycle. An age above 2.5 seconds invalidates the snapshot. The coroutine read groups now check IRQ completion and publish entire register groups only on success; the service checks all required groups before presenting valid data.

PM sleep-block bit 0x08 is held while ports are active or status is uncertain. Before STOP1, the PM hook asynchronously performs a fresh `SW6306_PortStatusLoad()` and requires its successful sequence to advance; only then can `SW6306_TransportPortsIdle()` use the just-refreshed RAM snapshot. LPSET and subsequent wake/unlock also yield without blocking unrelated coroutines. The inherited SW6306
configuration disables passive A-port VBUS insertion detection, so
a cable plugged into an inactive A port may not be detectable.

**Verify hardware configuration before connecting real loads:** the
current SW6306 BSP initialization includes 5S/4.2V cells,
5 mΩ battery shunt scaling, 100W input and experimental 105W output configuration following PB10A changes. These are existing driver
defaults, not independently confirmed PB10A battery/hardware specs.

### Key input (Collar-Radio Buttons DAS)

The four direction keys use the adapted `BSP/Buttons/buttons.c/.h`:
10 ms scans, 30 ms debounce, 1 s initial DAS hold and 200 ms repeat.
Inputs are enqueued for MiaoUI; OLED DMA busy frames do not lose events.
CONF does not repeat. A short press triggers ENTER **on release**; holding
for 700 ms produces one BACK with no ENTER on release. Presses begun
during the PM wake guard are suppressed until the key is released.

ICON pages map LEFT/RIGHT to item selection, UP/DOWN to MINUS/PLUS.
TEXT pages map UP/DOWN to item selection, LEFT/RIGHT to MINUS/PLUS.
Root BACK invokes `pm_api_force_sleep()` (UI_OFF / STOP1), following
IH01A/DPC27A.

The home tiles use 30x30 XBM assets. Powerbank has a custom 30x30 battery/bolt XBM icon;
the Radio page restores the original Collar-Radio 12px/24px fonts,
status XBM icons, FM dial, discovered-station markers and RTC footer.
The original shared 12px fusion-pixel menu font remains in place.

`SC7A20Algo_Init()` registers the independent async service; its PB02B 25 Hz, ±2 g,
high-resolution BDU setup is unchanged. The PB10A sensor interrupt is PB5,
not PB02B's PB8 shared interrupt. The experimental temperature-ADC
compatibility register is not yet board-verified.

### Restored Collar-Radio animations and radio sleep clock

The Radio page now preserves the **original** Collar-Radio visual behavior
from `Application/Applications/radio.c`, instead of replacing its
graphics with a static approximation.

- Main view: the original `disp_value1=12` offset moves the entire
  top signal/audio/battery status row in from above (`y - offset`)
  and the bottom time/date row in from below (`y + offset`) using
  the cursor PID. The central frequency and 43/45/47px station ruler
  keep their original coordinates.
- Five-row menu: original `y=20+i*10`, title/separator, fixed label
  at x=2 and dynamic value at x=74. The selected row has a 10px high
  XOR inverted highlight with separate PID-animated **Y position**
  and **text-width**, not a static full-row outline.
- No blocking `while(1)`: the original animation positions now persist
  across successive `Radio_AppRun()` calls. Each frame advances the
  existing MiaoUI `UI_Animation()` PID using the same tuned coefficients,
  stored in the firmware's Q10 fixed-point format. MiaoUI nominally
  polls at 20ms but OLED DMA can limit actual display frame cadence.
  The original Collar-Radio animated battery *fill* is driven by the
  cached SW6306 capacity when available; otherwise only its outline is drawn.

RDA5807 power policy: `RadioAlgo_Init()` queues the IRQ-driven
`RDA5807_AsyncBootSleep()` write with ENABLE=0. MCO is kept active
until the asynchronous power-down write succeeds or reports an error.
`RadioAlgo_BootSleepAcknowledged()` reports a successful boot power-down write.
The dedicated RadioServiceThread (5 ms) owns FM startup, tune/seek,
status polling, presets, power policy and I2C completion.
`ON` means playback regardless of MIC_INDET; `AUTO` means playback
only while the 40ms-debounced MIC_INDET input is low; `OFF` always
turns the receiver off. An active ON or plugged-in AUTO receiver
can play through STOP1 while MCO stays active; RTC wakes every
30 minutes for an invisible background battery/headset check.
On wake, Radio restores the PA8 clock selection from its own state.
The RTC keeps LSE running independently of PA8.

### I2C scanner restoration (Collar-Radio visual compatibility)

The original Collar-Radio I2C Scanner used a 1.5 s large-font
`I2C`/`Scanner` splash, followed by 128 cells in 16 columns x 8 rows
with `font_home_h6w4` (each address rendered as two 4px hex glyphs).
PB10A now preserves that UI exactly with the source font restored from
Collar-Radio. The inner blocking `while(1)` and 128-device tight loop
have been replaced by one address probe per MiaoUI frame. CONF rescans,
long CONF BACK exits, and rescan occurs automatically 5 seconds after
completion. For I2C bus safety, addresses 0x00-0x07 and 0x78-0x7F
are not probed; their visual matrix cells stay `--`.

### Radio MiaoUI WORD page (Collar-Radio five-item menu)

Radio main view: physical UP/DOWN controls RDA5807 0-15 volume;
LEFT/RIGHT selects lower/higher frequency according to Radio Mode:
Search (seek), Preset (saved stations, wraps), Frequency (100 kHz step).
CONF short release enters the five-row settings menu. In that menu
physical UP/DOWN selects items, CONF short release activates them:
Return, Power Mode (AUTO / ON / OFF), Radio Mode
(Search / Preset / Frequency), Clear + Full Seek (Preset) or
Save/Remove station (other modes), Exit Radio Page.

Power Mode AUTO (the boot default) turns the RDA5807 on when
MIC_INDET is low and turns it off when the jack becomes high, even
after leaving the Radio UI. ON operates without checking the headphone
input, including in STOP1; OFF always turns the receiver off. All
three modes keep normal PM auto-sleep policy independent of playback,
unless the explicit Sleep&Wake > Radio Keep Awake switch is enabled.
With Keep Awake enabled, ON prevents automatic sleep, whereas AUTO
does so only while headphones are inserted. Explicit root BACK sleep
is still a deliberate override of the idle timer.
CONF long BACK leaves the Radio WORD page, even from its settings menu.
UI sends commands through `RadioAlgo_Request()` and reads
`RadioAlgo_GetSnapshot()`; the dedicated 5 ms radio coroutine
polls the FSM and asynchronous RDA5807 I2C, including when the
Radio page is not visible.

### Fatal exception snapshot and reset

`APP/algorithm/fault_record.c` defines `g_fault_record` in the 256-byte
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

### Sensor service boundaries

- BSP/SC7A20, BSP/MMC5603 and BSP/BMP180 own register definitions,
  byte decoding and compensation; Core/I2C owns the LL IRQ transfers.
- APP/algorithm/sc7a20_algo, mmc5603_algo and bmp180_algo own asynchronous
  initialization, sampling cadence, cached results and PM preparation.
  Their I2C buffers and completion flags persist across scheduler calls.
- APP/MiaoUI/ui_conf.c only binds cached telemetry to Statistics/Sensors;
  no I2C request or sensor conversion runs in a drawing callback.
- Statistics/System displays VDDA in volts and die temperature in degrees
  Celsius from ADC ranks 4 and 3, averaged across 16 scans every 500 ms.
- Conversion timing, sensor scaling, STOP1 transitions and UI fields still
  require hardware verification; this edit has not been board-tested.

### Display flip orientation

The SSD1306 initializes in native `U8G2_R0`. Game sessions, Leveler and
Accel Calibration select `U8G2_R0` before each application draw. Menu draws
apply the current Screen Flip setting from the first frame after exit.

### SC7A20 gravity rotation and STOP1 wake (2026-11-20)

- Display > Screen Flip is an **auto-rotate permission**, default ON.
  It precedes Background in the Display menu. OFF always uses the
  SSD1306 native `U8G2_R0`; ON uses the SC7A20's
  confirmed orientation (`U8G2_R0` or `U8G2_R2`) in the MiaoUI thread
  only when the framebuffer is not owned by SPI DMA.
- The board's flat, screen-upright attitude is **Z≈-1000 mg**; inverting
  the box gives **Z≈+1000 mg**. Screen face down is **Y≈+1000 mg**,
  right edge down **X≈+1000 mg**. The 5 Hz estimator uses ±650 mg
  thresholds with about 2 s confirmation. Sideways attitudes do not
  update the orientation.
- Settings > Sleep&Wake contains Back, Sleep Options, Pickup Wake,
  Motion Wake. Sleep Options reproduces IH01A's mutually exclusive choices:
  No Auto Sleep, 15s, 30s, 1min, 2min, 5min, 10min, 15min, 30min.
  The default is 1min, matching PM's 60000ms default.
- Both wake switches default OFF. When either is enabled, SC7A20 runs
  25 Hz low power during STOP1. **Pickup Wake** uses PB02B's six-direction
  (all ±XYZ) 6D movement detection via AOI2, threshold 32 and duration 1;
  this is not restricted to ±Z. **Motion Wake** monitors high-pass XYZ
  movement via AOI1 (48 LSB / 2 ticks). Both route to the active-low
  SC7A20 INT1 wired to PB5/EXTI5. The EXTI ISR only latches a source
  flag; foreground PM hooks read/clear sources and restore 12-bit RUN.
- All SC7A20 sleep register writes/reads are IRQ-driven I2C and PM
  returns PENDING while transactions or HPF settling remain.
  The active-low INT input is checked before WFI to avoid a held-low
  line leaving STOP1 asleep after a falling edge was already cleared.
  Registers and current thresholds follow the PB02B LIS2DH-compatible
  implementation and require validation on the PB10A hardware; no
  hardware wake test has been performed.

### EXTI wake event ownership (PB10A)

The CubeMX GPIO configuration enables **falling-edge** EXTI inputs for:
- Five navigation keys: PC6, PA9, PA10, PA12, PA15, grouped as
  `PM_WAKE_KEY` (single source bit). Normal 10 ms button debouncing and
  action queuing remain in `indevService_10ms()`.
- SC7A20 INT1: PB5/EXTI5, `PM_WAKE_SC7A20`. Every falling edge
  wakes and refreshes the PM idle timer, even without Pickup/Motion
  monitoring armed. Only the SC7A20 PM device decides sensor register mode.
- `MIC_INDET`: PB0/EXTI0, `PM_WAKE_MIC_INDET`.
- `SUB_INDET`: PA7/EXTI7, `PM_WAKE_SUB_INDET`. Aux-A insertion
  state and voltage/current are still polled by `ADC_RTC_AuxAThread`
  at 500 ms, not sampled by the ISR.

The two insertion inputs have GPIO pull-ups and falling-edge interrupts,
so a high-to-low transition requests wake. They do **not** presently
handle unplug/rising edges or run connector-specific application logic.

All EXTI handlers set only bits in the volatile `s_irq_events` mask
through `pm_api_on_exti_event()`. CubeMX-generated LL handlers also
clear the pending flags. Every 50 ms, `pm_api_poll()` atomically takes
and clears this mask, refreshes the idle timer and passes an independent
copy into `s_app_events`. The foreground event service consumes that
copy via `pm_api_take_events()`. No external IRQ is filtered out by
the PM layer, including SC7A20 when wake monitoring is disabled.
Physical EXTI wakes the CPU from STOP1; the foreground PM task
subsequently controls UI and peripherals.

The PM transition and SC7A20 sleep hooks do not perform I2C access
inside interrupt handlers. Button contact bounce and sensor wake
thresholds must still be verified on real hardware.

### External event navigation / MiaoUI single bookmark (2026-11-20)

- `MiaoUI/core/ui.h` exports `UI_NavigateTo(ui,target,UI_NAV_PUSH|
  UI_NAV_REPLACE)` and `UI_NavigateBack(ui)`. The implementation stores
  one bookmark (originating item, viewport, cursor, scroll/option bars)
  in `ui_t`, not a dynamic stack. A temporary WORD application's BACK
  and Exit restore that original menu instead of its normal parent.
  External navigation from parameter editing calls its exit/commit hook
  before leaving. Applications may register `UI_SetLeaveHook()`.
- The PB10A request layer in `MiaoUI/ui_conf.c` owns the static `s_radio`
  and `s_powerbank` items and exposes `MiaoUi_RequestNavigate(target)`.
  That function records only a target ID; it never changes MiaoUI state.
  `MiaoUi_ProcessNavigate()` is called only by `MiaoUiThread`, when
  the OLED is ready, frame DMA idle and PM allows UI. The navigation
  core drops pre-navigation button actions so they cannot select items
  in the new page.
- Single-slot arbitration: Radio has priority over Powerbank; the latter
  waits 100 ms to allow a concurrent debounced headphone event to win.
  A higher-priority target can replace a temporary lower-priority
  application without losing the original bookmark. No nested stack.
- `Settings > System`: `Headphone Auto UI`, `Port Auto UI` and
  `Aux-A Auto UI` are three independent, default-OFF, RAM-only
  switches. They control page changes,
  not the Radio power policy or SW6306 output mode. Radio's MiaoUI leave
  hook resets its view state; `ON` keeps running in the background,
  while `OFF` remains off when merely opening the page.
- PB0/MIC_INDET falling-edge event goes through PM's foreground event
  consumer. After **40 ms** it confirms the pin is still low and submits
  Radio only if the auto-switch is enabled. One connection can trigger
  only once; returning to high re-arms the next insertion. A headset
  already plugged in at boot does not generate a synthetic event.
- SW6306's existing 100 ms staggered task needs no new I2C scan.
  Its successful `PortStatusLoad()` snapshot already contains TYPEC
  (0x19) and PORT_STA (0x1D). `SW6306_ConnectedPortMask()` uses C1/C2
  Source/Sink/CC/online flags and A1/A2 online flags, **not** SYS_STAT
  path-ON bits. The first successful read establishes a baseline, later
  complete reads identify 0->1 connections, and failed reads do not
  update it. The unpowered passive A-port case can remain undetectable.
- **PB5 is wired to SC7A20 INT1, not SW6306 INT.** SW6306 attachment
  must be confirmed by the polling coroutine; do not assume PB5 is
  a SW6306 port-change interrupt.
- All navigation actions, including returning from a temporary Radio
  screen, occur only on the UI coroutine. STOP1 wake does not invoke
  screen drawing or I2C in EXTI interrupt handlers.

Keil compile and physical insertion, simultaneous-event, UI animation
and STOP1 integration testing were not performed.

### MENU, Aux-A event and I2C-thread ownership (2026-11-21)

TEXT menu naming convention in `Create_MenuTree()` and item helpers:
`[Back]` for RETURN; ` Label` (one leading ASCII space) for a regular
entry, so it aligns with the 'B' immediately following '['; `-Submenu`
for a PARENTS entry, with '-' replacing that space. This also applies
to Time and Sleep Options. The source hierarchy is indented to mirror
the visible menu tree; ICON page label formatting is independent.

PA7 / SUB_INDET falling-edge wake events also receive a dedicated
40 ms low-level confirmation, one-shot insertion latch and high-level
re-arm in the power algorithm. Confirmed insertion enables Aux-A.
When the independent `Aux-A Auto UI` switch is enabled, a
confirmed new insertion requests `UI_TARGET_POWERBANK`. Both the
separately configured SW6306 Port Auto UI and Aux-A Auto UI switch
can navigate to the same Powerbank WORD page. Radio still has higher
priority over Powerbank. No ISR performs navigation or I2C.

The coroOS table has 10 deliberately grouped entries: OledServiceThread,
MiaoUiThread, SystemServiceThread; PowerManagerThread,
ADC_RTC_AuxAThread, PowerAlgo_SW6306Thread; RadioServiceThread;
SC7A20Thread, MMC5603Thread and BMP180Thread. Each sensor actor owns
its persistent buffer, completion flag, polling cadence and PM hooks.
The shared I2C controller runs IRQ-completed transfers, not blocking
calls inside cooperative tasks.

The Radio implementation follows Collar-Radio's `Application/Applications/radio.c`
in placement and key actions: top 7x9/6x9 status icons, 24-pixel
frequency value with a variable-width MHz placement, 43/45/47-pixel
station ruler, saved-station cursor, RTC footer, and animated XOR
selection in the five-row settings menu. Where SW6306 capacity is
known, its cached value fills the battery icon; the unknown case
shows the outline alone. The original FreeRTOS `while(1)` draw loop
is intentionally not copied.

The active FM call path is now:
- `BSP/RDA5807/rda5807m.c`: `RDA5807_Async*` operation-specific
  register commands, fixed-size serialized write queue, IRQ-backed
  I2C status reads, completion and mirror register maintenance.
- `APP/algorithm/radio_algo.c`: 5 ms coroOS state machine, startup,
  stop, FM seek/tune, sorted presets, mode/power policy, PM hook,
  RAM-only `radio_snapshot_t` and app command queue.
- `APP/functions/radio_app.c`: only drawing, UI animation, user
  input-to-command mapping and a non-hardware leave callback.

The other active sensor paths also use asynchronous BSP entry points:
SC7A20_AsyncRead/Write(), MMC5603_AsyncRead/Write(), and
BMP180_AsyncRead/Write(); SW6306 uses its existing protothread BSP
implementation. Legacy synchronous BSP helpers are retained as
compatibility APIs but are not called by these active coroutines.
No actual Keil build or on-device Radio validation has been done.

### RTC and Radio AUTO/ON low-power policy (production configuration)

The LSE-derived RTC WUT uses `RTC_WAKEUPCLOCK_CK_SPRE_16BITS` at
1 Hz and counter **1799** for one event every **30 minutes**. WUT is
armed only while a powered receiver is about to enter STOP1 with mode
`ON` or with mode `AUTO` and MIC_INDET currently asserted low.
No WUT is armed for an unpowered Radio or explicitly `OFF`.
The RTC ISR only latches an event; it never performs I2C, UI
navigation or screen drawing. RTC Alarm A/B remain separately reserved
for future BEEP alarms and the backup calendar survives MCU reset.

Radio mode logic (evaluated in the dedicated RadioServiceThread):
- `OFF`: Receiver off, no battery patrol WUT and no Radio sleep block.
- `AUTO`: Low MIC_INDET -> receiver may start; high MIC_INDET ->
  receiver must stop. The input is debounced for 40ms in RUN.
  It is also read **directly after RTC wake**: because removal has
  no rising-edge EXTI, the receiver remains active for at most the
  30-minute patrol interval after unplugging in STOP1. Power Mode
  remains AUTO; it does not get rewritten to OFF.
- `ON`: MIC_INDET has no authority over receiver startup, shutdown,
  RTC WUT or sleep block. The receiver continues playing unattended
  even with no headphones, unless battery protection trips.
- `Sleep&Wake > Radio Keep Awake`: RAM-only switch, default OFF.
  If enabled, ON prevents normal automatic idle sleep regardless of
  MIC_INDET; AUTO prevents idle sleep only with confirmed inserted
  headphones. Without the switch, active Radio never prevents PM
  idle timeout. Explicit user-requested sleep is still allowed.

Physical RTC wake leaves STOP1 but **does not wake OLED or MiaoUI**:
`PM_STATE_UI_OFF` is retained while RadioAlgo checks current mode,
MIC_INDET level and RDA5807 status. If AUTO detects unplugged
headphones it queues asynchronous shutdown immediately and skips
unneeded battery reads. If Radio is still permitted to play, PM
selectively calls the SW6306 resume hook; the existing
PowerAlgo_SW6306Thread performs a fresh `SW6306_ADCLoad()` and
`SW6306_CapacityLoad()` pair, returning a result to RadioAlgo.
After RadioAlgo reports completion (including any asynchronous
RDA5807 sleep), PM re-suspends SW6306 and transitions
`UI_OFF -> SLEEP_PREPARE -> DEEPSLEEP`. Other devices and OLED remain
suspended. External key/connector IRQ during this process aborts
background mode and runs the normal full PM resume path to RUN.
A background-check timeout also falls back to RUN for diagnosis
instead of silently skipping protection.

Battery shutdown on a fresh successful snapshot:
`capacity_pct == 0` or `vbat_mv < 14000` (5S x 2.8 V) forces
Radio Power Mode to OFF, cancels pending UI commands and powers down
RDA5807 asynchronously. Mode OFF remains until explicitly changed
after the low-voltage lock is cleared by capacity >5% and voltage
>=14,500 mV. Failed SW6306 I2C transactions are not assumed to be
0%; the patrol retries up to three fresh ADC+Capacity sequences.
If all fail, unattended Radio is turned OFF (fail closed).
During normal RUN the Radio coroutine also monitors the latest valid
SW6306 capacity/voltage cache every 500ms.

Radio UI battery indicator: >=70% four bars, >=50% three,
>=30% two, >=10% one, otherwise empty; at <=5% the entire 16x8
battery icon blinks with an 800ms phase, with unknown capacity
displayed as an unfilled outline.

SW6306-style `RDA5807_USE_PROTOTHREAD` dual-call compatibility
wrappers remain available for dedicated owner callers. The production
Radio coroutine uses the serial IRQ-backed `RDA5807_Async*` queue
instead; neither ISR nor MiaoUI directly operates the radio chip.

**Verification needed on hardware:** ON without headset, AUTO
plug/unplug while RUN, AUTO unplugged during STOP1 and eventual
30-minute shutdown without lighting OLED, STOP1 MCO continuity,
headset-dependent Keep Awake, and low-voltage/error protection.
No Keil full build or physical integration test was run in this edit.

### RTC wake-source flags and fixed LSE drift compensation (2026-11-21)

A STOP1 wake caused by any source (keys/EXTI, Alarm A/B, RTC WUT)
returns through `stm32_deepsleep()`. If the Radio WUT was armed,
`radio_wut_disarm()` deactivates the timer and WUT interrupt and
clears the WUTF hardware flag only after successful deactivation.
The HAL RTC IRQ handler clears WUTF on an actual WUT interrupt. The
software WUT cause remains latched until `pm_api_poll()` consumes
it *after* STOP1, so cancellation does not destroy wake-source
classification. On the next STOP1 preparation the 30-minute WUT is
rearmed from the beginning **only if**
`RadioAlgo_BackgroundWakeNeeded()` is true (ON with or without
headset; AUTO with the headphone still plugged in). OFF does not
arm it. Calendar Alarm A/B are not disabled by this WUT lifecycle.

`RTC_TAMP_IRQHandler()` dispatches both HAL alarm and WUT handlers.
The PM ISR callbacks now record **separate** alarm event bits
`PM_RTC_ALARM_A` / `PM_RTC_ALARM_B` and the WUT boolean
`s_rtc_wakeup_event`, independently of external GPIO
`s_irq_events`. The foreground may consume alarm reason bits via
`pm_api_take_rtc_alarm_events()`. Alarm A/B are not actually
scheduled yet; their default placeholder alarms remain disabled.
When an Alarm and WUT arrive simultaneously, **Alarm takes
precedence** and follows normal RUN wake rather than the silent
Radio background patrol. The alarm cause remains available to a
future alarm/BEEP task; simply recording an event does not play
the buzzer yet.

The RTC calendar uses the correct LSE prescaler product
`(127+1)*(255+1)=32768`. For the measured +10 s over 41 hours
(+67.75 ppm) the per-board provisional software correction is
`PB10A_RTC_SMOOTH_CALM=71U`, applied with
`HAL_RTCEx_SetSmoothCalib(&hrtc,RTC_SMOOTHCALIB_PERIOD_32SEC,
RTC_SMOOTHCALIB_PLUSPULSES_RESET,71U)`. This subtracts about
67.71 pulses per million. The call lives after
`RTC_ConfigureEvents:` inside CubeMX's `USER CODE BEGIN RTC_Init 2`,
so a valid backup calendar survives reset yet calibration is
reapplied each initialization. Smooth calibration adjusts calendar
rate, not original LSE oscillator frequency or PA8 MCO. Re-set
calendar time once and measure residual drift for multiple days;
the constant is not a measured guarantee for other boards.

### PB02B Leveler and six-face calibration (2026-11-21)

The **Tools** icon page now offers two WORD-page apps:

- `-Leveler` using Collar-Radio's original GPL-3.0
  `img_gradienter` 30x30 XBM icon. It adapts the PB02B
  `firmware/Applications/functions/leveler.c` 160x40 A8/LVGL
  drawing to PB10A's native **128x64** SSD1306 with per-row
  u8g2 span drawing (no additional framebuffer). There is **no
  right-hand X/Y/Z acceleration column**. Horizon tilt and vertical
  translation retain the original gravity-normal plane, plus
  XOR tick marks. The 43-degree entry/47-degree exit hysteresis,
  orientation-dependent white/black zenith view and dual XOR circles
  are retained. Each moving circle now has a 36px diameter, using
  half-pixel centers for exact raster width; the additional stationary
  zenith reference ring is removed.
  Current tilt is overlaid in the **bottom-right** using the
  project's default `UI_FONT` with a small degree-ring mark.
- `-Accel Calibration` using Collar-Radio's
  `img_gradienter_cal`. The PB02B six-face (not six independent
  accelerometer axes) procedure is ported with the same threshold
  and stability rules: X-/X+/Y-/Y+/Z-/Z+ with each candidate held
  for 10 additional fresh 200ms intervals (about 2 seconds).
  No partial face capture updates runtime calibration. When all
  faces validate, derive each axis' zero midpoint and
  slope=2000mg/(raw positive-negative span); the three sensors'
  calibration setters receive the six validated results together.
  Success is briefly displayed and the app exits automatically;
  any key can exit early before completion.

The existing `SC7A20Thread` remains the **only I2C user**:
`SC7A20Algo_GetSample()` exports a coherent RAM copy of both
unscaled codes and calibrated mg with a sequence number. Leveler
requests 25Hz sampling (matching sensor ODR) with 0.2 EMA smoothing;
calibration keeps 5Hz sampling. Normal orientation detection still
uses its original 5Hz/2-second hysteresis even while the leveler
is running. Both apps hold a distinct PM idle block (0x20) and
temporarily enforce native R0 display orientation; their MiaoUI
onLeave hooks always restore normal sampling, PM policy and
auto-rotation eligibility.

**Calibration retention (corrected for PB10A power hardware):**
PB10A has no independent backup-domain supply. Calibration therefore
does **not** use RTC DR1..DR4, whose contents would be lost on a full
VDD power-down just like ordinary SRAM. Instead, a 16-byte
`s_calibration_noinit` record shares the Keil linker scatter file's
reserved `RW_NOINIT` section with `g_fault_record` (32 bytes).
The scatter currently reserves the **last 256 bytes of 36 KiB SRAM**
for this area. The calibration consists of three signed 16-bit zero
codes and three unsigned 16-bit Q14 slopes, plus a 16-bit magic/version
and CRC16-CCITT. The marker is written last to reject interrupted
updates, and every boot checks the marker, CRC and plausible slopes.
`SC7A20Algo_Init()` recovers it after reset without asking the RTC.
It is retained only if SRAM stayed powered (e.g. software reset,
NRST, and some watchdog resets). **A full power cycle or a debugger
that clears RAM discards the record**; this is not nonvolatile storage.
DR0 remains the existing RTC calendar marker, but DR1..DR4 are now
unused by the SC7A20 app. There is no Flash erase/write or extra I2C
owner.

Both MiaoUI items, their source icons, the new
`APP/functions/tools_motion_app.c/.h`, `sc7a20_algo.c/.h`
and MDK project source list are included in the same patch.
Hardware calibration and front-panel visual tests are still needed.

### Radio readout, DMA-safe menu mosaic and PB10A leveler axes

The Radio page always renders its most recent frequency and the 43/45/47
pixel station ruler even when the RDA5807 hardware is powered down.
It only shows "Seeking..." during an active search and "Tuning..."
during an active tune; it no longer overwrites the frequency with
"Radio OFF". The top status row already reports headphone connection.
This is display-only and does **not** start the receiver.

The menu mosaic transition repair was extracted **only** from existing
`feature/miaoui-adaptive-games-20261121` at
`775a99934d92fd1fc1a8684f9f75c097c08c3a1c`.
`UI_Disapper()`'s eight back-to-back writes were replaced with an
eight-step randomized dissolve, one OLED DMA frame per existing
20 ms `MiaoUiThread` pass. Navigation is committed only after the
final mosaic frame has finished transmission. External UI navigation
cancels the effect cleanly. The adaptive scheduler, game changes and
branch history were **not** merged, and no new branch was created.

PB02B's `leveler.c` explicitly defines X=-1g horizontal
(top white/bottom black), Y=+1g vertical (left white/right black),
Z=-1g zenith (black background/white XOR circles). PB10A's
user-measured native mounting is different: a flat device with the
screen in its normal orientation gives native **Z=-1g**; with the
screen face down, **Y=+1g**; with the right edge down, **X=+1g**.
`SC7A20Algo` now publishes a separate, calibrated leveler-space
vector `level_mg={native_Z,-native_X,native_Y}`, preserving
native `raw[]` and `mg[]` for SC7A20 rotation, statistics and
six-face calibration. After physical testing the horizon
raster was corrected separately: top-white/bottom-black in the normal
pose, original downward translation preserved, and horizon roll
compensation opposite to case tilt. The plumb/zenith mapping and
six-face sensor axes are unchanged.
The six-face app's physical placement labels follow PB10A:
UP=Z-, DOWN=Z+, LEFT=X+, RIGHT=X-, face-up=Y-, face-down=Y+.


**Leveler display correction (2026-11-21, hardware follow-up):**
PB10A normal upright level-X=-1g is **white above, black below**
(the previous black-above change was incorrect). For the horizon only,
the scanline plane is
`f=(levelY/h)*(px-63)+(levelX/h)*(py-31)+50*levelZ`.
For the normal upright pose its boundary remains
`py=31+50*levelZ`: raising the device still moves the black horizon
downward. Right-edge-down roll now tilts the displayed horizon in the
**opposite** direction to device rotation, preventing the doubled
apparent angle. This change leaves the already-correct zenith/plumb
axis map untouched. The two XOR zenith discs remain exactly **36px**
in diameter with no stationary reference ring, and screen-face-up
zenith retains a **white** background. The bottom-right default-font
angle and degree mark now use `U8G2` draw color 2 (**XOR**) directly
over the scene, **without an opaque backdrop**. The default u8g2
font mode is SOLID and would still write a black glyph-bounding box;
`level_angle_footer()` now switches to TRANSPARENT font mode only
while drawing the digits and restores the previous mode afterward.
The degree marker is a **3x3 hollow four-point cross**
(top/left/right/bottom, center clear); each pixel is drawn exactly
once, avoiding XOR cancellation in `u8g2_DrawCircle(radius=2)`. The caller's
previous drawing color is also restored. Black/white horizon and
zenith visual behavior are unchanged.
When the two zenith XOR discs precisely overlap they cancel, by design.

### ADC 16-scan averaging (2026-11-21)

Modeled on Collar-Radio's `Application/Algorithms/system_statistic.c`
(`OVERSAMPLE_RATE=4` in the Collar-Radio source), PB10A now runs
**16** ADC1 DMA scans per 500 ms display batch, spaced about 25 ms
apart. With Aux-A enabled, the display batch takes samples from the
5 ms protection scans. Each scan reads PA0 current,
PA5 voltage, die temperature and VREFINT in the original rank order.
The 16 raw codes for each rank are accumulated as `uint32_t`;
the nearest-integer mean is taken **before** the VREFINT calibration
and the voltage/current/temperature conversions, so all derived values
use a single averaged VDDA. A batch is published only after 16
complete scans. The former 50 ms software DMA timeout has been
removed per project policy. ADC errors discard the affected batch.
All scan waits yield via Protothreads to other coroOS tasks. Sampling is software
triggered as before, with no CubeMX initialization changes.

This is software mean filtering rather than MCU ADC hardware
oversampling. It improves rejection of uncorrelated sample noise but
does not correct calibration, source impedance, persistent interference
or aliasing. Real-device validation is still required.
