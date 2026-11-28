# PB10A 硬件局部原理图预览

该目录保存用户提供的九张硬件原理图/概念拓扑图片，供 [硬件架构与模块解读](../../Hardware/Hardware_Architecture.md) 在各对应小节就近引用。

**格式：** 为便于文档浏览，预览图从用户上传的 PNG 转为黑白高对比度 PNG（1-bit）；除 Aux-A 截图为适合网页阅读缩小至 1181 × 440 px 外，其余保持原图尺寸；**图形结构保留，颜色信息已丢失**。原始彩色图、元件数值、引脚和网络连接仍应以作者的 EasyEDA Pro 工程、[网表](../../Hardware/Preview/) 以及当前实装 PCB 为准；预览图不是可编辑电气设计源文件。

| 文件 | 图纸范围 | 解读章节 |
|---|---|---|
| [00_FSBB_topology.png](00_FSBB_topology.png) | 四开关双向 Buck-Boost 原理示意 | §4 |
| [01_RDA5807_audio_frontend.png](01_RDA5807_audio_frontend.png) | FM、耳机音频与插拔检测 | §9 |
| [02_I2C_sensors_and_pullups.png](02_I2C_sensors_and_pullups.png) | SC7A20 / MMC5603 / BMP180、I2C 上拉 | §8 |
| [03_AuxA_independent_output.png](03_AuxA_independent_output.png) | TPS54302、IP2161、INA199、Aux-A | §10 |
| [04_3V3_aux_supply.png](04_3V3_aux_supply.png) | U6 TPS54302 3V3 电源 | §5 |
| [05_OLED_power_and_interface.png](05_OLED_power_and_interface.png) | OLED SPI 与 CPEN 电源控制 | §7 |
| [06_MCU_reset_debug_buzzer.png](06_MCU_reset_debug_buzzer.png) | MCU、LSE、SWD、复位、蜂鸣器 | §6 |
| [07_BMS_and_battery_gauge.png](07_BMS_and_battery_gauge.png) | BM3451、保护 MOS、电量指示 | §3 |
| [08_WLED_boost_driver.png](08_WLED_boost_driver.png) | SY7301A LED 升压恒流驱动 | §11 |

图片按上传顺序对应整理，文件名使用功能用途，避免与 `Images/Testing` 中的实测示波器截图混淆。
