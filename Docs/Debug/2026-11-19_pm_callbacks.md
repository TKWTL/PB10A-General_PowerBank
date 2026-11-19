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
| 200 | power-adc | `APP/services/power_algo.c` | STOP1 前停止 ADC/DMA，再拉低 PA6/SUB_EN；唤醒后拉高 PA6，恢复 ADC 采样；失败时取消休眠 |
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
- 附加 A 口与 ADC 采样统一由 `power_algo.c` 管理。
  PA6/SUB_EN 上电默认 HIGH，RUN/UI_OFF 保持 HIGH，STOP1 前 LOW，
  唤醒后重新 HIGH。STOP1 期间附加 A 口不供电。
- SW6306 处于已连接/充放电状态时保持自治运行；芯片未初始化或实时
  端口查询失败时不写 LPSET。该策略避免无意断开供电协议，不代表
  SW6306 所有工作模式均达到最低功耗。
- MMC5603 只有在 `Probe` 成功且连续测量/ODR 已开启时才写
  CTRL2=0、ODR=0；恢复保存值。BMP180 无独立待机命令，单次转换
  完成后自行空闲。不重置传感器。
- 恢复回调应尝试恢复所有子设备，即使其中某个 I²C 事务失败。
  强制休眠在错误回退至 RUN 后会清除 forced-expiry 标志，避免连续循环；
  OLED CPEN 低期间不发送非电源命令。
- 回调涉及实际电源和时钟：需分别实测自动熄屏、按键中止休眠、
  STOP1 唤醒，以及充电/放电过程的连续性。**当前仅源码静态检查，
  未完成 Keil 编译和硬件功耗测量**。

## 收音机动画移植与 MCO 电源时序

**回归问题**：先前将 Collar-Radio 的阻塞式 `Radio_Run()` 简化为
PB10A 的单次 `Radio_AppRun()` 时，把 `disp_value1` 的上/下状态栏 PID
滑入，以及菜单选项框的 Y/宽度双 PID 动画替换成了静态控件。这是
功能遗漏，不是 STM32G070 或 coroOS 的能力限制。

**恢复方法**：保留原版菜单坐标、图标、XOR 选项框和 PID 参数，把
`while(1)` 内每轮的绘制/输入处理改为一次 `Radio_AppRun()` 调用。
三个动画状态 `s_slide_q`、`s_cursor_y_q`、`s_cursor_w_q` 保存在
静态结构中，由 MiaoUI 的 Q10 定点 `UI_Animation()` 每帧推进；
不用长时间独占调度器，也不需要浮点库。

**功耗检查点**：Radio 服务初始化在 `MX_I2C1_Init()` 之后；
初始化期间临时提供 LSE MCO，向 RDA5807 的 0x02 寄存器写
`ENABLE=0` 后停止 MCO；状态可通过
`RadioAlgo_BootSleepAcknowledged()` 检查。播放前开启 MCO，
UI_OFF/STOP1 期间关闭（含错误状态的尽力关断），唤醒后不
自动开启。播放过程中的 I²C 错误也不得持续占用 MCO 或
休眠阻止标志。I²C 关断未确认时不能声称射频芯片已关断。

**实机验证**：需观察进入 Radio 的上下状态栏同时滑入，菜单中
连续移动选择项时框体位置及宽度均有过渡；同时测量 MCO
引脚在初始化后、Radio 工作时、UI_OFF 和 STOP1 唤醒后的波形。
