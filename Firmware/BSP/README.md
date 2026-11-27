# Firmware/BSP · 板级支持

板级设备驱动和硬件相关的基础支持，面向板载设备及其传输接口；片上外设初始化和通用传输状态机仍在 `Core/`。

典型模块：

- `Display/ssd1306_display.c/.h`：SSD1306 SPI1 DMA 异步发送、屏幕供电 CPEN、帧与电源状态；u8g2 绘图核心在 `u8g2/`。
- `Buttons/buttons.c/.h`：物理按键消抖、长按与连发；逻辑按键和屏幕方向映射在 MiaoUI。
- `SW6306/`：电源芯片寄存器驱动，整数/浮点、Protothread/阻塞接口配置见 [README](SW6306/README.md)。
- `RDA5807/`：FM 芯片异步写队列与状态读取，兼容接口保留；搜台判定和电台表由 APP 持有。
- `SC7A20/`、`BMP180/`、`MMC5603/`：设备寄存器访问及采样转换；设备服务、标定与 PM 策略在 APP。
- `Time/millis.c/.h`：独立 SysTick 毫秒计数。
- `WDT/wdt.c/.h`：WWDG 调度看门狗、休眠边界喂狗与 NOINIT 调度现场。

蜂鸣器 PWM 和提示音队列位于 `APP/framework/beep_service.c/.h`，不再保留独立 `beep_hw`。

规则：

- 通过 `Core/` HAL/LL 实现寄存器级操作，并向上提供设备驱动 API；不直接决定应用业务策略。
- 引脚配置以 CubeMX `.ioc` 和 `Core/Inc/main.h` 为准；设备驱动使用相应绑定，移植时检查 GPIO、外设、DMA、IRQ 及电源时序。
- 新增模块后必须在 Keil 工程里加入 `Application/BSP` 分组，并把本目录加入 `IncludePath`。


### Millisecond timebase

`Time/millis.h` declares `volatile uint32_t millis`; it is incremented once per SysTick IRQ directly inside `Core/Src/stm32g0xx_it.c` USER CODE. `Millis()` returns the counter, and `Millis_Delay(ms)` provides a blocking delay without HAL_GetTick/HAL_Delay. The HAL tick is kept for HAL internal users. On STOP1 SysTick is suspended, so `millis` intentionally does not measure sleep duration; use RTC for absolute elapsed time. Use unsigned subtraction for timeout checks across counter wrap-around.


### I2C1 shared IRQ transport

The LL master engine lives in `Core/Src/i2c.c` USER CODE and is serviced by
`I2C_TransferIRQ()` and foreground `I2C_AsyncService()` before each scheduler
round. I2C1 uses PB8/PB9, open drain. `ASYNC_I2C_Transmit/Receive` and
`I2C_AsyncProbe` admit one transfer at a time; callers keep data buffers
and completion flags alive until DONE/ERROR. Return 0 means not admitted;
return 1 may already report ERROR. The priority argument is reserved.
`I2C_RegTransferPT` yields until admission and completion. Legacy synchronous
`I2C_RegRead/Write` and `I2C_TransactionBegin/End` are for non-yielding
foreground operations only; never hold that lock across a coroutine yield.
No HAL I2C interrupt callback or DMA transport is used on this path.

The transfer deadline is 30 ms, STOP/abort deadline 10 ms. A failed abort
or 50 ms of BUSY/low lines while idle and unlocked invokes bounded Bus
Clear: disable I2C, release open-drain pins, pulse SCL at most nine times,
issue STOP if possible, reset I2C1 and restore CubeMX initialization.
Do not force a stuck line high. Failed recovery retries after 1 s; the
failed request is not replayed, ordinary NACK alone is not a recovery
trigger, and the MCU is not reset by the bus service. Debug counters:
`g_i2c_recovery_attempts`, `g_i2c_recovery_failures`. PM must not enter STOP1
while `I2C_BusIsLocked()` is true.

### WWDG

`COROOS_USE_WATCHDOG=1` selects the hardware window watchdog. At 64 MHz PCLK,
128 prescaler and 0x7F reload/window give 524.288 ms nominal timeout.
`Watchdog_Init()` starts it after CubeMX initialization, before APP init.
Only a completed round with advancing Millis refreshes it before idle;
ordinary Sleep retains its clock. Before STOP1, refresh and clear
`RCC_APBSMENR1.WWDGSMEN`, then restore the clock gate immediately on return.
No Option Byte programming. The 16-byte NOINIT live trace is captured by
FaultRecord at boot on WWDGRSTF; it records the last phase/thread, not a
proven cause. `g_watchdog_max_round_ms` excludes ordinary scheduler idle
and STOP time. See [APP overview](../APP/README.md) for recovery details.
WWDG cannot detect complete APB clock loss or every yielding logical stall.
