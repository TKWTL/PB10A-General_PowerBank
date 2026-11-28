# PB10A · General PowerBank

> **项目状态：主要功能已集成，进入功能收敛与长期可靠性验证阶段。** 实测性能与验证边界见整机测试报告。

## 产品文档

- [用户说明书](Docs/User_Manual.md)：功能与操作（待完善）。
- [组装指南](Docs/Assembly_Guide.md)：制造与复刻（待完善）。
- [整机测试报告](Docs/Test_Report.md)：实际性能、测试工况及待测项。
- [软件实现](Firmware/README.md) · [硬件资料](Hardware/README.md) · [文档导航](Docs/README.md) · [发布流程](RELEASE.md)。

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
| `Firmware/APP/` | 应用层：`framework/`（基础时间、蜂鸣器与低功耗服务）、`algorithm/`（设备状态机/传感器/功率算法）、`functions/`（应用页面） |
| `Firmware/BSP/` | 板级支持：OLED、按键、外部功能芯片、Millis 与 WWDG 等板级设备支持 |
| `Firmware/MDK-ARM/` | Keil 工程、启动文件、RTE、调试器配置；产物输出到 `Objects/`、`Listings/` |
| `Docs/` | 用户说明书、组装指南、整机测试报告 |
| `Hardware/` | 原理图、PCB、BOM、结构件 |
| `Images/` | `Assembly/` 装配、`Product/` 成品、`Testing/` 测试波形与截图 |
| `相关数据手册/` | 关键器件数据手册，按器件类别分目录；已入库供远程 agent 查阅，版权属原厂，发布前须从历史移除（见 `RELEASE.md`） |

分层以 `APP → BSP → Core/Drivers` 为主要方向；外接设备寄存器驱动位于 `BSP/`，系统基础服务位于 `APP/framework/`，设备算法位于 `APP/algorithm/`。`Core` 与 `Drivers` 保留生成代码和厂商库，避免引入应用业务依赖。

## 固件开发快速开始

1. 用 STM32CubeMX 打开 `Firmware/PB10A-General_PowerBank.ioc` 改外设，重新生成代码（工程名与目录保持不变）。
2. 用 Keil MDK-ARM 打开 `Firmware/MDK-ARM/PB10A-General_PowerBank.uvprojx`，Build 目标 `PB10A-General_PowerBank`。
3. 新增 `.c/.h` 放到对应分层目录后，需在 Keil 里加入匹配的 Group 并补 `IncludePath`；否则编译不到。

详细分层约定与命名规范见 [`Firmware/README.md`](Firmware/README.md)。
