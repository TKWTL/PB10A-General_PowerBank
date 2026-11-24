# Firmware · STM32G070KBTx

**状态：基础硬件联调阶段。** 已完成 ADC、蜂鸣器、FM 等板级功能的初步验证；应用菜单、按键映射和部分功耗行为仍需整机回归测试。调试经验见 [`Docs/Debug/`](../Docs/Debug/README.md)，早期 OLED 记录见 [`Docs/OLED_BRINGUP.md`](../Docs/OLED_BRINGUP.md)。

## 分层与依赖

```
APP (framework / algorithm / functions)
       └──► BSP  ──► Core / Drivers
            设备驱动   CubeMX 生成层 + ST 库
```

| 目录 | 放什么 | 不放什么 |
|---|---|---|
| `Core/` | CubeMX 生成的外设初始化、中断向量和时钟；I²C HAL 适配 API 与回调在对应 `USER CODE BEGIN/END` 区 | 应用业务策略；修改 CubeMX 管理的初始化段 |
| `Drivers/` | STM32G0xx HAL/LL 与 CMSIS 头文件 | 任何手写代码 |
| `APP/framework/` | 基础系统服务：RTC 时间/日历编辑、蜂鸣器 PWM/提示音队列、低功耗与调度框架 | 页面级功能逻辑 |
| `APP/algorithm/` | 设备状态机、ADC/功率/FM/传感器算法、配置与故障记录；纯计算部分尽量独立于 HAL | 与页面直接耦合的 UI 绘图 |
| `BSP/` | 板载人机与调试接口的板级封装：OLED、按键、蜂鸣器、调试串口/CLI | 外接功能芯片协议 |
| `MDK-ARM/` | Keil 工程、启动文件、RTE、调试器配置 | 源码 |

## 约定

- **CubeMX 是外设配置的唯一真源**：改引脚/外设一律改 `.ioc` 后重新生成，不手改 `Core/` 初始化段。
- **命名**：文件与目录用小写下划线（`power_path.c`）；对外接口统一加模块前缀（`pp_`、`batt_`、`oled_`）；私有函数加 `static`。
- **头文件**：一个模块一个同名 `.h`，只暴露必要接口；跨层调用只允许自上而下。
- **新增源文件后必须同步工程**：在 `MDK-ARM/PB10A-General_PowerBank.uvprojx` 里加入对应 Group，新目录要补 `IncludePath`，否则编译不到。
- **目录深度**：`Firmware/` 下最多两层，不再新增同义目录（例如不要再建 `Module/`、`Src2/`）。

## Keil 工程

| 项 | 值 |
|---|---|
| 工程 | `MDK-ARM/PB10A-General_PowerBank.uvprojx` |
| 目标 | `PB10A-General_PowerBank` |
| 输出 | `MDK-ARM/Objects/`（`Objects/`、`Listings/` 不入库） |
| 宏定义 | `USE_HAL_DRIVER`, `USE_FULL_LL_DRIVER`, `STM32G070xx` |
| 设备内存 | IROM 0x08000000 / 128 KB，IRAM 0x20000000 / 36 KB；布局见 `MDK-ARM/Ld/PB10A-General_PowerBank.sct`（末 256 B 为 `.noinit` 复位不清除区） |
| 现有分组 | `Application/MDK-ARM`、`Application/User/Core`、`Drivers/STM32G0xx_HAL_Driver`、`Drivers/CMSIS`、`::CMSIS` |

已新增 `Application/APP`、`Application/MiaoUI`、`Application/BSP` 和 `ThirdParty/u8g2` 分组，包含 OLED 最小系统所需的源码。`MiaoUI` 保留库原有子目录，作为目录深度约定的例外。未加入不需要的电源管理和外设驱动。

### Code formatting

PB10A-maintained C sources use 4 spaces, same-line opening braces, and
function/argument declarations without an automatic line-width limit.
See repository [`.clang-format`](../.clang-format) and
[`.clang-format-ignore`](../.clang-format-ignore). Do not autoformat
STM32CubeMX-managed `Core/`, ST HAL `Drivers/`, third-party u8g2,
or generated fonts and bitmaps. Major logical sections use blank lines
and SW6306-style starred comment dividers.

## 第三方组件与 AI 辅助声明

第三方库、字体的原作者、仓库内许可证位置，以及 AI 辅助开发/调试/文档说明统一见 **[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)**。其中包括 MiaoUI、u8g2、Fusion Pixel Font / Ark Pixel Font、Protothreads 1.4、STM32Cube HAL/LL 和 CMSIS；AI 辅助包括 OpenAI ChatGPT 与 DeepSeek v4.1f。各上游作品仍保留原许可证与版权声明。

### 2026-11 目录整理与 ADC 时序

`APP/services/` 已撤销：`beep_hw` 与 `beep_algo` 合入 `APP/framework/beep_service.c/.h`；`time_algo` 与 `time_service` 合入 `APP/framework/time_service.c/.h`，对外 `Time_*`、`RTC_*`、`beep_*` 接口保持不变。其余设备/故障/系统逻辑移至 `APP/algorithm/`，`Peripheral/` 的旧占位 README 也已移除。Keil 的 `.uvprojx` 与 `.uvoptx` 源文件路径和 IncludePath 已同步。

ADC 保持每 **500 ms** 发布一组数据，但组内由原来的 4 次扩展至 **16 次**四通道 DMA 扫描，扫描间隔 **25 ms**（15 次共 375 ms），最后计算 16 次原始值均值；不加入新的 DMA 超时或失败恢复流程。外设配置由现有 CubeMX 工程保持。

### Firmware/LIB algorithms

Hardware-independent RLE decompression (four control modes, WRITE/XOR) and
32-bit power-of-two SPSC FIFO are in [LIB](LIB/) and included in the Keil
project. RLE now stores Pac-Man/Bomber/Invaders page images losslessly,
and FIFO now manages Beep Service 32-bit pointer records. Its 1/2/4-byte
APIs also support byte-oriented UART rings. IH01A's integer-only
`mini_format.c/.h` replaces PB10A application `snprintf` calls; protocol
labels use bounded direct copying instead of `%.15s`. Measure real linked
size after Keil compilation rather than assuming theoretical gains. This byte RLE is
unrelated to u8g2 font RLE. See [LIB/README.md](LIB/README.md).

Radio now reuses the 12px main UI font for identical ASCII glyphs. The 24px
Spleen font is subsetted for radio and I2C scanner messages; see
[font subset notes](APP/MiaoUI/fonts/README.md).
