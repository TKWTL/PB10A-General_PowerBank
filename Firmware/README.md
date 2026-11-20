# Firmware · STM32G070KBTx

**状态：基础硬件联调阶段。** 已完成 ADC、蜂鸣器、FM 等板级功能的初步验证；应用菜单、按键映射和部分功耗行为仍需整机回归测试。调试经验见 [`Docs/Debug/`](../Docs/Debug/README.md)，早期 OLED 记录见 [`Docs/OLED_BRINGUP.md`](../Docs/OLED_BRINGUP.md)。

## 分层与依赖

```
APP  ──►  BSP  ──►  Peripheral  ──►  Core / Drivers
应用逻辑   板级器件   外接芯片驱动     CubeMX 生成层 + ST 库
```

| 目录 | 放什么 | 不放什么 |
|---|---|---|
| `Core/` | CubeMX 生成的外设初始化、中断向量、时钟；手写代码只写在 `USER CODE BEGIN/END` 之间 | 业务逻辑、模块间调用 |
| `Drivers/` | STM32G0xx HAL/LL 与 CMSIS 头文件 | 任何手写代码 |
| `APP/framework/` | 系统启动流程、时钟/时间基准、事件/任务调度、低功耗状态机、看门狗 | 具体外设寄存器操作 |
| `APP/algorithm/` | 协议解析、滤波与标定、状态估计、参数换算等纯计算模块（尽量不依赖 HAL） | 直接读写 GPIO |
| `BSP/` | 板载人机与调试接口的板级封装：OLED、按键、蜂鸣器、调试串口/CLI | 外接功能芯片协议 |
| `Peripheral/` | 外接功能芯片的寄存器级驱动：SW6306V、BM3451、INA199 等，只依赖 HAL | 业务策略、UI |
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
