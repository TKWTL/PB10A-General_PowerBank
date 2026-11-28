# Firmware · STM32G070KBTx

**状态：主要功能已集成，进入功能收敛与长期可靠性验证阶段。** ADC、蜂鸣器、FM 等基础硬件已有板级联调记录；当前固件包含多口电源管理、FM、传感器、双 RTC 闹钟、游戏、中英文界面及故障恢复。最新源码的所有组合与低功耗行为仍需整机回归，本文不代表完成新一轮编译或实测。调试经验见 [`Debug/`](Debug/README.md)，早期 OLED 记录见 [`Debug/OLED_BRINGUP.md`](Debug/OLED_BRINGUP.md)。

用户操作与复刻资料见 [产品文档](../Docs/README.md)。Keil 远程同步约定见 [GIT_WORKFLOW.md](GIT_WORKFLOW.md)。

## 分层与依赖

```
APP (framework / algorithm / functions / MiaoUI)
       ├──► Scheduler (coroOS + Protothreads 1.4)
       ├──► LIB (整数算法、FIFO、RLE、CRC)
       └──► BSP ──► Core / Drivers
            设备驱动   CubeMX 生成层 + ST 库
```

| 目录 | 放什么 | 不放什么 |
|---|---|---|
| `Core/` | CubeMX 生成的外设初始化、中断向量和时钟；I²C LL 中断状态机、SPI/ADC DMA 适配在对应 `USER CODE BEGIN/END` 区 | 应用业务策略；修改 CubeMX 管理的初始化段 |
| `Drivers/` | STM32G0xx HAL/LL 与 CMSIS 头文件 | 任何手写代码 |
| `APP/framework/` | 基础系统服务：RTC 时间/日历编辑、双闹钟、蜂鸣器 PWM/提示音队列、低功耗框架 | 页面级功能逻辑 |
| `APP/algorithm/` | 设备状态机、ADC/功率/FM/传感器算法、配置与故障记录；纯计算部分尽量独立于 HAL | 与页面直接耦合的 UI 绘图 |
| [`BSP/`](BSP/README.md) | OLED、按键、SW6306、RDA5807、SC7A20、BMP180、MMC5603、Millis、WWDG 与 u8g2 | 应用业务策略 |
| [`Scheduler/`](Scheduler/) | coroOS 与第三方 Protothreads 1.4，固定顺序合作式调度 | 设备和页面业务 |
| [`LIB/`](LIB/README.md) | FIFO、字节 RLE、整数格式化、CRC32 和数学辅助 | UI/应用依赖 |
| [`APP/functions/`](APP/functions/README.md) | 独立应用及游戏 | 外设传输所有权 |
| `APP/MiaoUI/` | 菜单、显示交互、字体及图标 | 芯片协议状态机 |
| `MDK-ARM/` | Keil 工程、启动文件、RTE、调试器配置 | 源码 |

## 约定

- **CubeMX 是外设配置的唯一真源**：改引脚/外设一律改 `.ioc` 后重新生成，不手改 `Core/` 初始化段。
- **命名**：文件与目录用小写下划线（`power_path.c`）；对外接口统一加模块前缀（`pp_`、`batt_`、`oled_`）；私有函数加 `static`。
- **头文件**：一个模块一个同名 `.h`，只暴露必要接口；跨层调用只允许自上而下。
- **新增源文件后必须同步工程**：在 `MDK-ARM/PB10A-General_PowerBank.uvprojx` 里加入对应 Group，新目录要补 `IncludePath`，否则编译不到。
- **目录深度**：沿用已有职责目录；MiaoUI、游戏、BSP 设备及第三方库可保留必要子目录。不再新增同义目录（例如 `Module/`、`Src2/`）。

## 外设移植

[main.h](Core/Inc/main.h) 的 `USER CODE BEGIN EM` 区集中定义运行时绑定。先在 CubeMX 更新引脚、时钟、DMA 请求、ADC 扫描顺序和中断，再更新对应宏。

| 用途 | 当前绑定 |
| --- | --- |
| OLED 发送 | SPI1、DMA1 通道 1 |
| FM、传感器和 SW6306 总线 | I²C1 LL 中断，PB8 SCL / PB9 SDA |
| FM 参考时钟 | PA8 MCO，LSE ÷1 |
| 蜂鸣器 | TIM3 PWM |
| 调度看门狗 | WWDG，STOP1 前后切换低功耗时钟门控 |
| ADC 扫描 | ADC1、DMA1 通道 2，依次采集 Aux-A 电流、Aux-A 电压、片内温度和 VREFINT |

重新生成后，在对应的 `USER CODE` 区保留 SPI / ADC 的 DMA 地址初始化。DMA 中断调用 `DMA_TransferIRQ()` 并传入该向量的 IRQ 编号。I²C 中断调用 `I2C_TransferIRQ()`。

## Keil 工程

| 项 | 值 |
|---|---|
| 工程 | `MDK-ARM/PB10A-General_PowerBank.uvprojx` |
| 目标 | `PB10A-General_PowerBank` |
| 输出 | `MDK-ARM/Objects/`（`Objects/`、`Listings/` 不入库） |
| 宏定义 | `USE_HAL_DRIVER`, `USE_FULL_LL_DRIVER`, `STM32G070xx`；`SC7A20_USE_FLOAT_API=0`, `BMP180_USE_FLOAT_API=0`, `SW6306_USE_FLOAT_API=0`, `COROOS_USE_IDLE_SLEEP=1`, `COROOS_USE_WATCHDOG=1` |
| 优化 | Arm Compiler 6，`-Oz` + LTO（以工程实际选项为准） |
| 设备内存 | IROM 0x08000000 / 128 KB，IRAM 0x20000000 / 36 KB；布局见 `MDK-ARM/Ld/PB10A-General_PowerBank.sct`（末 256 B 为 `.bss.noinit` 复位不清除区） |
| 现有分组 | `Application/MDK-ARM`、`Application/User/Core`、`Drivers/STM32G0xx_HAL_Driver`、`Drivers/CMSIS`、`Application/APP`、`BSP/Buttons`、`Application/MiaoUI`、`Application/LIB`、`Application/BSP`、`BSP/u8g2`、`::CMSIS` |

工程已包含完整应用、PM、设备驱动和必要的第三方子集。检查链接占用时以 `.map` 的 `LR_IROM1`、`RW_IRAM1`、`RW_NOINIT` 区域为准；`Code + RO-data + RW-data` 是 ROM 数据统计，可能与实际加载区存在对齐差异。SRAM 包含预留栈；静态链接占用不能证明运行时栈峰值。AXF 的调试信息不计入烧录容量。

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

ADC 每 **500 ms** 发布一组数据，对间隔约 **25 ms** 的 **16 次**四通道 DMA 扫描取均值。Aux-A 启用时由同一个 ADC 协程每 **5 ms** 扫描一次，用单次样本判断欠压关断，显示仍从这些扫描中按原节奏取样。ADC 采样时间和通道配置沿用现有 CubeMX 工程。

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

Radio shares the 12px main UI font; the 24px Spleen subset serves radio
and scanner messages. The current shared 12px font contains 96 Latin/degree
and 264 Han glyphs (8175 B). Language is RAM-only, Chinese by default;
see [font subset notes](APP/MiaoUI/fonts/README.md).

## 当前核心功能与可靠性机制

- **调度**：`APP/app_task.c` 注册 10 个固定顺序协程；UI、OLED、系统、PM、ADC、SW6306、Radio 与三种传感器各自推进，不使用 FreeRTOS。共享 I2C 服务在每轮调度之前运行。
- **FM**：AUTO 随耳机插拔运行，ON 不依赖耳机，OFF 关闭。87–108 MHz / 100 kHz；硬件 SEEK 后对命中及相邻两个频点分别取两次有效 RSSI 的较低值，邻频须改善至少 2 个计数或替代无效原分数；预设全扫向上，双向 Search 优先探测搜索方向。原五项菜单不变。正常调谐先显示目标并等待 STC/READCHAN 匹配。PA8 直接输出 LSE，RTC 平滑校准不修改 FM 参考频率。
- **低功耗**：STOP1 前按设备状态机挂起；FM 后台播放时由 30 分钟 RTC WUT 检查电池与耳机，正常后台巡检不点亮 OLED。双 RTC 日历闹钟与 WUT 独立；报警播放有独立的 PM 锁。
- **I2C 恢复**：30 ms 传输 / 10 ms 中止期限，失败中止或空闲总线异常持续 50 ms 后尝试 Bus Clear 和 RCC 复位；失败 1 s 后重试，不自动重放写入。
- **WWDG**：64 MHz PCLK 下标称 524.288 ms；完整调度轮且 Millis 前进才喂狗，普通 Sleep 保持监控，STOP1 前喂狗并关闭 WWDGSMEN，退出后恢复。无需修改 Option Bytes。NOINIT 保存故障与调度现场，复位后可检查，断电不保证保留。

模块细节与现有板级验证记录见 [APP/README.md](APP/README.md)、[BSP/README.md](BSP/README.md)；用户应用见 [functions/README.md](APP/functions/README.md)。后续以编译资源、搜台重复性、I2C 恢复成功率、最大调度轮时长、STOP1 功耗及唤醒稳定性为回归指标。
