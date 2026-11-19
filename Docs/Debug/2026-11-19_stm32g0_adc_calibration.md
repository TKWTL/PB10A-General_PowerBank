# STM32G070 ADC 校准后禁用超时（PB10A）

- **日期**：2026-11-19
- **位置**：`Firmware/APP/services/power_adc_algo.c` 调用的
  `HAL_ADCEx_Calibration_Start(&hadc1)`
- **芯片**：STM32G070KBTx，ADC1，软件触发四通道、DMA Normal
- **状态**：板级 ADC 采样已经恢复；本案例记录 Keil 调试过程与
  本项目有效的 CubeMX 修复，不要求修改 ST HAL 源码。

## 现象

Statistics → Power 曾出现 `ADC Err 4`，`ADC HAL` 和
`DMA HAL` 为 0，PA0/PA5 原始采样值保持 0。随后 Keil 发现
`PowerADC_Init()` 中 `s_cal_failed=1`，进入校准失败处理分支。

在该分支暂停时的实测寄存器/变量：

| 变量 | 观测值 | 解释 |
|---|---|---|
| `hadc1.State` | `0x00000013` | READY (0x01)、BUSY_INTERNAL (0x02)、ERROR_INTERNAL (0x10) |
| `hadc1.ErrorCode` | `0x00000001` | `HAL_ADC_ERROR_INTERNAL` |
| `ADC1->CR` | `0x10000000` | `ADVREGEN=1`，暂停时 `ADEN=0` |
| 校准循环超时 | **未命中** | 不是 `ADC_CALIBRATION_TIMEOUT` 的返回路径 |
| 校准结束后禁用超时 | **命中** | `ADC_DISABLE_TIMEOUT` 内第二层判断的 `return HAL_ERROR` |

`ADC Err 4` 是应用层状态值，不能将其直接解释为 HAL 错误码 4。
校准失败留下的 `HAL_ADC_STATE_ERROR_INTERNAL` 状态位可能在后续 DMA
完成回调时令 HAL 走入错误路径，而 `HAL_ADC_Start_DMA()` 清除的
`ErrorCode` 可能已归零。这解释了最初看似矛盾的屏幕数值。

## Keil 定位流程

1. 在 `PowerADC_Init()` 的 `HAL_ADCEx_Calibration_Start()` 调用之后
   设断点，Watch 中添加 `s_cal_failed`、`hadc1.State`、
   `hadc1.ErrorCode`、`ADC1->CR`。
2. 若校准失败，打开
   `Drivers/STM32G0xx_HAL_Driver/Src/stm32g0xx_hal_adc_ex.c`，
   在 `HAL_ADCEx_Calibration_Start()` 的两个不同超时分支设断点。
3. 实际命中的是**八次校准结束后**、写入校准系数并请求禁用 ADC
   所经过的 `ADC_DISABLE_TIMEOUT`，不是循环内的
   `ADC_CALIBRATION_TIMEOUT`。
4. 可继续观察 `calibration_index`、`ADC1->CR`、
   `ADC1->ISR`、`tickstart` 与 `HAL_GetTick()`。
   应在失败位置而不是仅在应用层返回后观察寄存器，
   因为 ADC 可能随后完成禁用。

## 根因分析与适用范围

旧配置为 HSI 16 MHz、`ADC_CLOCK_ASYNC_DIV8`（ADC 时钟约 2 MHz）。
此版本 ST HAL 在校准后执行 `LL_ADC_Enable()`，其后的软件等待
只有在异步分频系数达到 DIV16 时才执行；DIV8 不走该延迟。
因此可能发生 ADC 尚未完全准备好，就写入 `CALFACT` 并执行
`LL_ADC_Disable()`，最终触发禁用超时。

这属于根据调用路径、时序和已知问题提出的**原因分析**，
并非从一次 `CR=0x10000000` 快照直接证明 ADC 内部竞争时序。
ST 官方公开记录了相同类型的 HAL 缺陷：
[STM32CubeG0 issue #44 — Delay missing from ADC calibration](https://github.com/STMicroelectronics/STM32CubeG0/issues/44)。

## 本项目解决方案（已在板上确认采样恢复）

在本地 CubeMX 修改 ADC1：

| 参数 | 旧值 | 新值 |
|---|---|---|
| `ClockPrescaler` | `ADC_CLOCK_ASYNC_DIV8` | `ADC_CLOCK_ASYNC_DIV1` |
| `SamplingTimeCommon1` | 12.5 cycles | 39.5 cycles |
| `SamplingTimeCommon2` | 39.5 cycles | 79.5 cycles |

更新 `Firmware/PB10A-General_PowerBank.ioc` 并重新生成
`Firmware/Core/Src/adc.c` 后，恢复使用**未修改的 ST HAL 源码**。
用户已确认 ADC 正常采样，当前 `main` 保留了 CubeMX 更新和
ST HAL 还原提交（`2da12f25`、`1afc3522`）。

注：更长的采样时间有利于采集高阻抗模拟输入及内部测量通道，
但本次已定位的校准禁用超时主要与 ADC 使能/禁用时序有关，
不能将增加采样时间单独认定为此故障的修复原因。

## 后续验证与注意事项

- 校准调用返回 `HAL_OK`，`s_cal_failed=0`，
  `hadc1.State` 不包含 `HAL_ADC_STATE_ERROR_INTERNAL`。
- Statistics → Power 中 `ADC Err=0`，PA0/PA5 有效，保持
  500 ms 周期触发，STOP1 唤醒后仍可采样。
- DIV1 在 PB10A 目前已有效，但仍应检查 ADC 工作频率是否满足
  芯片数据手册条件；更换时钟、HAL 版本或优化级别后需重新验证。
- **不要直接编辑 `Firmware/Drivers/STM32G0xx_HAL_Driver/` 的
  STM32 HAL 库**。此路径可能在更新 STM32Cube 包时被覆盖。
  若将来必须修补 HAL 缺陷，应优先检查新版官方 HAL 是否修复，
  或采用明确维护的独立兼容层并保留回归测试。
