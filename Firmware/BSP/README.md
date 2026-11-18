# Firmware/BSP · 板级支持

板载人机与调试接口的板级封装，面向"这块板子上焊了什么"，而非"这颗芯片怎么配"。

典型模块：

- `oled.c/.h`：显示屏驱动与几何绘制（具体控制器时序可下沉到 `Peripheral/`）
- `key.c/.h`：按键扫描、消抖、长按/组合键事件
- `buzzer.c/.h`：蜂鸣器与提示音
- `console.c/.h`：调试串口、命令解析（CLI）

规则：

- 只依赖 `Peripheral/` 与 `Core/`；不写业务策略，不直接决定设备状态机。
- 引脚号、有效电平、上拉方式等板级差异必须集中在本层，不散落到 `APP/`。
- 新增模块后必须在 Keil 工程里加入 `Application/BSP` 分组，并把本目录加入 `IncludePath`。


### Millisecond timebase

`Time/millis.h` declares `volatile uint32_t millis`; it is incremented once per SysTick IRQ directly inside `Core/Src/stm32g0xx_it.c` USER CODE. `Millis()` returns the counter, and `Millis_Delay(ms)` provides a blocking delay without HAL_GetTick/HAL_Delay. The HAL tick is kept for HAL internal users. On STOP1 SysTick is suspended, so `millis` intentionally does not measure sleep duration; use RTC for absolute elapsed time. Use unsigned subtraction for timeout checks across counter wrap-around.


### I2C1 shared IRQ transport

`I2C_TransactionBegin()`/`I2C_TransactionEnd()` delimit the full logical operation (e.g. SW6306 register-group switch, RDA5807 initialization, BMP180 ID+calibration, MMC5603 low-power sequence). Each transfer uses `HAL_I2C_*_IT`, completed through existing `I2C1_IRQHandler()` and BSP HAL callbacks. The foreground caller blocks waiting for those interrupts, with a finite timeout and interrupt-driven abort. `I2C_IsReady` issues a zero-byte address-only interrupt-driven transmit, as supported by the bundled STM32G0 HAL implementation. Do not call from IRQ, during STOP1, or from a coroutine that expects to yield while holding a bus lock.
