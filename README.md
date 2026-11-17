# PB10A · General PowerBank

> **项目状态：OLED 最小移植阶段（尚未实测）。** STM32CubeMX 外设初始化骨架已接入 SSD1306 OLED 的 SPI1 DMA 驱动、MiaoUI/u8g2 和 coroOS；尚未实现电源管理、PD 业务和其它外设驱动。参见 [`Docs/OLED_BRINGUP.md`](Docs/OLED_BRINGUP.md)。

## 基本信息

| 项目 | 值 |
|---|---|
| MCU | STM32G070KBTx（Cortex-M0+，LQFP32，128 KB Flash / 36 KB RAM） |
| 系统时钟 | 64 MHz（HSI 16 MHz → PLL×8 /2） |
| 固件库 | STM32Cube FW_G0，HAL + LL 混用（`USE_HAL_DRIVER`, `USE_FULL_LL_DRIVER`） |
| 工具链 | STM32CubeMX + Keil MDK-ARM V5（ArmClang / AC6） |
| 已配置外设 | GPIO、DMA1、ADC1、CRC、I2C1、RTC（LSE 32.768 kHz）、SPI1、TIM1、TIM3；HAL 时基走 SysTick（不再使用 TIM7 时基） |
| 许可证 | GNU AGPL-3.0-only（见 `LICENSE`） |

## 工程结构

| 路径 | 职责 |
|---|---|
| `Firmware/PB10A-General_PowerBank.ioc` | CubeMX 工程定义，**外设/引脚配置的唯一真源** |
| `Firmware/Core/Inc`、`Core/Src` | CubeMX 生成层；只允许改 `USER CODE BEGIN/END` 段内的代码 |
| `Firmware/Drivers/` | ST HAL/LL + CMSIS，第三方代码，不手改 |
| `Firmware/APP/` | 应用层：`framework/`（系统、时间、事件、低功耗）、`algorithm/`（协议、滤波、参数换算） |
| `Firmware/BSP/` | 板级支持：OLED、按键、蜂鸣器、调试串口等板载人机与调试接口 |
| `Firmware/Peripheral/` | 外接功能芯片的寄存器级驱动（SW6306V、BM3451、INA199 等），不含业务逻辑 |
| `Firmware/MDK-ARM/` | Keil 工程、启动文件、RTE、调试器配置；产物输出到 `Objects/`、`Listings/` |
| `Docs/` | 实验报告、协议与接口说明、测量原始数据 |
| `Hardware/` | 原理图、PCB、BOM、结构件 |
| `Images/` | `Assembly/` 装配、`Product/` 成品、`Testing/` 测试波形与截图 |
| `相关数据手册/` | 关键器件数据手册，按器件类别分目录；已入库供远程 agent 查阅，版权属原厂，发布前须从历史移除（见 `Docs/RELEASE.md`） |

分层依赖方向固定为 `APP → BSP → Peripheral → Core/Drivers`，即下层不得反向引用上层，`Core`、`Drivers` 不得包含任何业务头文件。

## 快速开始

1. 用 STM32CubeMX 打开 `Firmware/PB10A-General_PowerBank.ioc` 改外设，重新生成代码（工程名与目录保持不变）。
2. 用 Keil MDK-ARM 打开 `Firmware/MDK-ARM/PB10A-General_PowerBank.uvprojx`，Build 目标 `PB10A-General_PowerBank`。
3. 新增 `.c/.h` 放到对应分层目录后，需在 Keil 里加入匹配的 Group 并补 `IncludePath`；否则编译不到。

详细分层约定与命名规范见 [`Firmware/README.md`](Firmware/README.md)。
