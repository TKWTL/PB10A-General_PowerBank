# Firmware/APP · 应用层

业务逻辑所在地，不直接操作寄存器。按职责分两类：

| 目录 | 放什么 |
|---|---|
| `framework/` | 系统启动流程、时间基准/Uptime、事件或任务调度、低功耗状态机、看门狗、日志 |
| `algorithm/` | 协议解析、滤波与标定、状态估计、参数换算等纯计算模块 |

规则：

- 只调用 `BSP/`、`Peripheral/` 暴露的接口，不包含 HAL 头文件之外的外设细节；**`algorithm/` 尽量不依赖 HAL，保证可主机侧单测**。
- 新增模块 `xxx.c/.h` 后必须在 Keil 工程里加入 `Application/APP` 分组。
- 业务状态机与硬件时序分开：时序在 `Peripheral/`，策略在 `APP/`。
