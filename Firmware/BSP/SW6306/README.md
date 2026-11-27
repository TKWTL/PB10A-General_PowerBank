# SW6306

数值与传输选项独立配置，编译驱动和调用方时使用相同的宏值。

| 宏 | `0` | `1` |
| --- | --- | --- |
| `SW6306_USE_FLOAT_API` | 整数接口 | 保留原浮点接口 |
| `SW6306_USE_PROTOTHREAD` | 阻塞式 I²C | Protothread 等待 I²C 中断 |
| `SW6306_NTC_TEMP_CALC_EN` | 关闭温度缓存计算和整数查表 | 启用温度缓存计算 |

PB10A 的 Keil 工程选择整数，应用通过现有的 Protothread 任务访问驱动。驱动头文件默认 `SW6306_USE_PROTOTHREAD=1`、`SW6306_USE_FLOAT_API=1`、`SW6306_NTC_TEMP_CALC_EN=1`；Keil 明确覆盖浮点接口为 0。FPU 由目标工程的编译选项配置。

## 采样电阻和电流校准

整数与浮点模式的校准参数独立，均在 `sw6306.h` 中编译期配置。
电压与电流接口仍返回整数 mV、mA；浮点模式仅在换算时使用浮点。

| 参数 | 整数模式 | 浮点模式 |
| --- | --- | --- |
| BAT 校准 | `BATT_CURRENT_NUM/DEN = 1/1` | `BATT_RSHUNT_NOMINAL / BATT_RSHUNT_ACTUAL = 5.0f/5.0f`（mΩ） |
| BUS 校准 | `BUS_CURRENT_NUM/DEN = 497/446` | `BUS_CURRENT_GAIN = 1.0f` |

BAT 增益为标称采样电阻与实际采样电阻之比，两个电阻值必须为正。
PB10A 整数 BUS 增益来自 4.97 A 参考仪表读数、4.46 A SW6306 未补偿读数的
单点实测，约为 1.11435；它不是对实际电阻值的认定。浮点模式
默认 1.0，便于移植到其他硬件。

| 路径 | 当前实现 |
| --- | --- |
| ADC 镜像 | IBAT = raw × 5 mA，IBUS = raw × 4 mA；不补偿 |
| `ReadIBAT` / `ReadIBUS` | 在读镜像时乘对应增益，四舍五入并饱和至 65535 mA |
| `ReadIBattLimit` | 对实时电池限流寄存器译码后的 mA 值乘 BAT 增益 |
| Init 的 CHG_IBAT / DCHG_IBAT | 目标电流除以 BAT 增益，再按 100 mA/LSB 四舍五入、饱和至 255 |
| 浮点库仑计能量 | 原始码 × 标称 mWh/LSB × BAT 增益；Rounded 接口最后取整 |
| 整数库仑计能量 | 先四舍五入标称 mWh，再乘 NUM/DEN 并取整 |

BUS 校准只修正电流读数；不修改 BUS 硬件限流、PD/PPS/UFCS 档位、
外部电流注入 `IextSet` 或 ADC 原始镜像。BAT 默认 1/1，保持原限流设置。

## PB10A 运行时约束

SW6306 由唯一的应用协程持有，不允许并发执行另一套 SW6306 Protothread。底层使用 `Core/Src/i2c.c` 的 LL 中断传输与前台恢复服务，而非 HAL DMA/IT。30 ms 传输超时及 10 ms 中止超时由总线层处理；恢复不会重放失败写入。调用方检查本次操作结果并由业务状态机决定重试或重新初始化。普通 NACK 不触发总线清除。

普通 STOP1 休眠以 `SW6306_PowerPathsIdle()` 判断充放电及 C1/C2/A1/A2 功率通路均关闭。PT 调用方必须先取得新鲜有效的端口状态镜像；CC 连接、端口在线及历史故障标志本身不阻止休眠。

Statistics 的 Battery Voltage / Battery Current 使用同一轮 ADC 镜像，电流采用已校准的 IBAT 读数；失效的镜像不显示为有效零值。BUS 补偿仍不改变协议电流档位或硬件限流。

## RTOS 移植

阻塞模式的传输函数在调用任务内等待完成。定义 `SW6306_BLOCKING_PORT_HEADER` 为带引号的端口头文件名并提供以下接口。省略端口头文件时使用 PB10A 的 `I2C_RegRead`、`I2C_RegWrite` 和 `Millis_Delay`。

| 端口宏 | 约定 |
| --- | --- |
| `SW6306_BLOCKING_READ(addr, reg, data, len)` | 同步寄存器读，返回 0 表示成功 |
| `SW6306_BLOCKING_WRITE(addr, reg, data, len)` | 同步寄存器写，返回 0 表示成功 |
| `SW6306_BLOCKING_LOCK()` | 获取总线递归互斥锁，返回 0 表示成功 |
| `SW6306_BLOCKING_UNLOCK()` | 释放一层递归互斥锁 |
| `SW6306_BLOCKING_DELAY_MS(ms)` | 任务延时，计入 tick 相位后至少等待指定毫秒数 |

`addr` 使用驱动的 `SW6306_I2C_ADDR`（默认左移一位的地址 `0x78`），`reg` 为 8 位，`len` 为 16 位。RTOS 工程用同一把互斥锁协调该总线的其他设备。由一个任务管理 SW6306 镜像，或由调用方串行化操作、镜像读取及成功检查。调用前用 `SW6306_OperationReset()` 清除上次结果，返回后用 `SW6306_LastOperationOK()` 检查。

## 整数 NTC

PB10A 使用 10 kΩ、B3380 热敏电阻。默认表覆盖 −40 至 125 °C，每 5 °C 存一个阻值。整数路径按 ADC 原始码和 20、40 或 80 µA 激励电流做线性插值，返回 0.1 °C。零码、满量程、无效电流和查表越界返回 `SW6306_NTC_INVALID_C10`。

读取温度前先成功执行 `SW6306_ADCLoad()` 和 `SW6306_StatusLoad()`，以获得同一轮的 VNTC 和 INTC。可通过 `SW6306_CalcNTCTemp_c10()` 直接计算，或先执行 `SW6306_NTCTempLoad()` 再读缓存。芯片温度、NTC 电压和库仑计能量分别用 0.1 °C、0.1 mV 和取整后的 mWh 表示。

从仓库根目录生成表，修改参数可适配其他热敏电阻。浮点路径对应的参数在 `sw6306.h` 中配置。

```sh
python3 Firmware/BSP/SW6306/gen_ntc_table.py --r25 10000 --beta 3380
```

## 初始化与协议配置

放电 UVLO 为单节 3.0 V（5S 整包约 15.0 V），单节迟滞保持 0.3 V。UVLO 中断与容量学习流程仍启用。

固定初始化寄存器使用顺序表，共用写入/位修改等待点；页切换、
解锁、动态功率与电池限流、Source Capability 重播仍保留原有时序。
PD/PPS/UFCS 电流档位直接计算寄存器编码，Protothread 只在首次进入
时计算并保存待写字节，阻塞路径使用局部缓冲。协议开关查表保留
原有方向限制、掩码及有效电平。现有 I²C 失败标志阻止后续配置写入。

`SW6306_ReadProtocolCode()` 返回 QCSTAT 协议编号（16 表示未知）；
`SW6306_ReadProtocol()` 保留旧字符串接口。
