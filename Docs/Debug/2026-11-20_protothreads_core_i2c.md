# Protothreads / coroOS 回退及 Core I²C 适配分层

日期：2026-11-20

## 目标与来源

PB10A 原来手写的 `coroOS` 是不完整的 Protothreads 风格实现；
SW6306、SC7A20 等设备驱动预留了 `SW6306_USE_PROTOTHREAD` 分支，
但该分支需要 `struct pt`、`PT_SEM_*`、`PT_SPAWN` 等功能。

已把 [Handheld-Fan](https://github.com/TKWTL/Handheld-Fan)
的 `coroOS/pt1.4/` 原始 Protothreads 文件移入
`Firmware/Scheduler/pt1.4/`，保留其开源版权声明。PB10A
`coroOS.h` 保留现有 `THRD_*` 接口和六个协程注册入口，
但底层现在调用 `PT_BEGIN` / `PT_WAIT_UNTIL` /
`PT_SPAWN`。与 Handheld-Fan 一样，它仍是**无栈协程**：
自动局部变量不会跨 `THRD_YIELD` 或 `THRD_SPAWN` 保存。
重复调用的子协程不可同时使用同一个静态 `subpt`。

## I²C 分层迁移

原 `Firmware/BSP/I2C/bsp_i2c.c/.h` 已删除，并从
Keil 项目移除。现有 I²C HAL 包装实现和中断回调搬到
`Firmware/Core/Src/i2c.c` 的 `USER CODE BEGIN 1` 区，
接口声明在 `Firmware/Core/Inc/i2c.h` 的 USER CODE 区。
CubeMX 生成的外设配置没有手动修改。

- `ASYNC_I2C_Transmit/Receive()` 和 `I2C_AsyncProbe()`
  提交事务即返回：返回 1 表示已接收请求，返回 0 表示当前繁忙。
  完成标志从 PENDING(0) 由 HAL 中断回调改为 DONE(1) 或 ERROR(2)。
- 提交后数据缓冲区和完成标志**必须保持存活**，直到中断结束
  或发生有界超时/Abort；不能传入会被后续调用覆盖的普通局部数组。
- `I2C_AsyncService()` 在每次 `APP_TaskRunOnce()` 处理超时；
  I²C 状态转移由 STM32 HAL 的中断实现，不复制 Handheld-Fan
  的 I²C 寄存器级状态机。
- I²C Scanner 已使用异步探测，保留原版 16×8 逐格显示；
  STOP1 内置设备钩子会在 `I2C_BusIsLocked()` 为真时等待。
- 原有 `I2C_RegWrite/Read` 等同步 API 为 SC7A20、MMC5603、
  BMP180、RDA5807 和尚未适配的 SW6306 保留。**这些调用仍在
  自身协程中同步等待，期间其他 coroOS 线程无法运行**。
  本次工作不能描述为“所有 I²C 访问均非阻塞”。

## 为什么 SW6306_USE_PROTOTHREAD 暂不打开

原 SW6306 驱动的 protothread 条件编译不能直接使用。
具体有三个独立错误类别：

1. `SW6306_I2C_Receive()` 在异步分支返回的是“是否接受
   事务”（成功为 1），旧同步分支 `I2C_OK` 是 0。
   `ADCLoad/StatusLoad/PortStatusLoad/CapacityLoad` 中的
   `!= I2C_OK` 不能直接沿用；必须改成
   “提交 → 协程等待完成标志 → 判断错误码”。
2. `ADCLoad`、`StatusLoad` 等存在自动数组、计数变量，
   `THRD_SPAWN` 后不能保证其值仍在有效栈帧。
   必须迁移为持久静态/实例数据，并消除对重入的依赖。
3. `SW6306_RegsetSwitch` 和 `SW6306_ByteModify` 都
   试图在外层函数已持有 `PT_SEM_WAIT` 时重新获取同一
   信号量，普通 Protothreads 计数信号量并不支持递归获取，
   必须明确单一 SW6306 服务所有权或重构锁边界。

强行定义 `SW6306_USE_PROTOTHREAD` 会造成编译失败、死锁
或读写缓冲区失效，因此目前宏**保持未定义**。
完成驱动转换时需要同步改造 `PowerAlgo_Service100ms()`、
SW6306 服务线程与 PM 的 `PENDING` 回调，确保 STOP1 不会
切断半途中的 I²C 事务。

## 105W POSET 的硬件限制

`SW6306_OUTPUT_POWER_MAX` 已根据当前需求设为 **105W**，
对应理论的 `21V × 5A`、`20V × 5A` 广播上限；
输入 `SW6306_INPUT_POWER_MAX` 仍为 **100W**。

但 SW6306V 公开数据手册将芯片标称输入/输出能力描述为
**最高 100W**。105W 设置属于超过标称功率的实验性配置，
并不能证明芯片可以在 21V/5A 下连续稳定输出，甚至
`POSET` 可能在内部受限。要先确认 0x4F 写入与 0x51
实际能力读回，再谨慎开展分级负载及热验证。

## 回归检查

1. Keil 编译全量工程，确认 pt1.4 头文件和 `Core` 回调唯一；
2. 扫描器在某个地址超时/无应答时，按键与 OLED 协程仍运行；
3. I²C 超时/Abort、重复进入扫描器，以及 STOP1 事务排空；
4. 现有 SW6306、传感器、FM 同步兼容调用不与 Core 事务冲突；
5. 20V/21V 档位重新读 PDO/UFCs 能力，确认 0x4F、0x51 读回；
6. 转换 SW6306 协程前禁止启用宏，不能用“编译通过”代替逐
   通道失败与多任务/低功耗时序测试。
