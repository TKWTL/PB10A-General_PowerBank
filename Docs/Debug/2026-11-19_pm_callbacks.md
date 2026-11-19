# 低功耗回调与外设上/下电顺序（2026-11-19）

## 框架

`APP/framework/pm_device.h` 的 prepare/suspend/resume 回调返回：

- `PM_HOOK_DONE`：本阶段已完成，可以调用下一个设备；
- `PM_HOOK_PENDING`：尚未完成，主循环下次 `pm_api_poll()` 继续从当前设备执行；
- `PM_HOOK_ERROR`：取消本次休眠并尝试逆序恢复所有设备，错误设备名保留在 `pm_device_last_error()`。

`pm_controller` 不再假定一次调用就完成所有动作。完成前不进入
STOP1；有唤醒事件时可中止尚未完成的休眠并逆序恢复。恢复期间阻止
MiaoUI 绘图。仅平台节点在所有 suspend 回调结束后进入 WFI。

顺序（prepare/suspend 升序，resume 降序）：

| 次序 | 节点 | 文件 | 行为 |
|---|---|---|---|
| 80 | sensors | `APP/services/sensor_algo.c` | SC7A20 ODR=0、MMC5603 待机，BMP180 保持空闲；恢复原配置 |
| 90 | sw6306 | `APP/services/power_algo.c` | 仅实时确认端口全部空闲才请求 LP；不关闭充/放电 |
| 110 | rda5807 | `APP/services/radio_algo.c` | UI_OFF 前关闭运行中 FM，关闭 MCO；唤醒由应用决定是否启动 |
| 200 | power-adc | `APP/services/power_adc_algo.c` | STOP1 前停止 ADC DMA，恢复后继续采样 |
| 230 | oled | `APP/framework/pm_device_builtin.c` | 等待 DMA 序列完成 |
| 250 | stm32g070 | `APP/framework/pm_device_builtin.c` | STOP1 中暂停 Tick/关闭内部测量路径；唤醒后恢复 |

## OLED 时序（CPEN → charge pump → display）

BSP `ssd1306_display.c` 的 SPI DMA 是异步的，PM 回调只提交请求并
检测完成：

- 初次初始化：**CPEN 高 → 等待至少 1 ms → SSD1306 初始化命令**
  （其中 `0x8D 0x14` 使能电荷泵、末尾 `0xAF` 点亮 OLED）。
- 关闭：待帧 DMA 完成 → `0xAE` 关闭显示 →
  `0x8D 0x10` 关闭电荷泵 → 命令 DMA 完成 →
  **CPEN 拉低**；无需在电荷泵关闭后额外延时。
- 开启：**CPEN 拉高 → 至少 1 ms → `0x8D 0x14` →
  `0xAF`**，命令 DMA 完成后允许 UI 绘制。
- PM 对 OLED 等待最长 500 ms；如果仍未完成，不进入 STOP1。
  初次初始化的原 30 ms CPEN 延时已按板级要求改为 1 ms。

## 限制及边界

- 当前 `BSP/I2C` 使用 `HAL_I2C_*_IT`，但对调用者同步等待完成，
  每笔最多 30 ms 加错误时 10 ms 中止等待；PM I²C 操作尚不是真正的
  **跨 coroOS 调度的非阻塞传输**。不能在 I²C 锁持有期间 yield。
- SC7A20 本次进入的是 ODR=0（Power-down），**不能在 STOP1
  依赖其运动中断唤醒**。如果要支持运动唤醒，应单独定义保留
  中断的低速测量配置，并对 HR/LPEN 模式兼容性实测。
- SW6306 处于已连接/充放电状态时保持自治运行；芯片未初始化或实时
  端口查询失败时不写 LPSET。该策略避免无意断开供电协议，不代表
  SW6306 所有工作模式均达到最低功耗。
- MMC5603 只有在 `Probe` 成功且连续测量/ODR 已开启时才写
  CTRL2=0、ODR=0；恢复保存值。BMP180 无独立待机命令，单次转换
  完成后自行空闲。不重置传感器。
- 回调涉及实际电源和时钟：需分别实测自动熄屏、按键中止休眠、
  STOP1 唤醒，以及充电/放电过程的连续性。**当前仅源码静态检查，
  未完成 Keil 编译和硬件功耗测量**。
