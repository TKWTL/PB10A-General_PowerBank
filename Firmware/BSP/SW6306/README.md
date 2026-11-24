# SW6306

数值与传输选项独立配置，编译驱动和调用方时使用相同的宏值。

| 宏 | `0` | `1` |
| --- | --- | --- |
| `SW6306_USE_FLOAT_API` | 整数接口 | 保留原浮点接口 |
| `SW6306_USE_PROTOTHREAD` | 阻塞式 I²C | Protothread 等待 I²C 中断 |
| `SW6306_NTC_TEMP_CALC_EN` | 关闭温度缓存计算和整数查表 | 启用温度缓存计算 |

PB10A 的 Keil 工程选择整数，应用通过现有的 Protothread 任务访问驱动。驱动头文件默认保留浮点接口。FPU 由目标工程的编译选项配置。

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
