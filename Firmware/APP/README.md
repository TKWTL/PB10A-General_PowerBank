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
