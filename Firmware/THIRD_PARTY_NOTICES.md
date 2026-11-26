# 第三方组件声明与 AI 辅助创作说明

本目录固件由本项目维护者集成、移植和修改；**引用的第三方代码或字体仍分别遵守其原始许可证**。本仓库顶层的 [AGPL-3.0-only](../LICENSE) 不会覆盖或替换第三方组件的独立许可与署名要求。

## 第三方库及字体

| 组件 | 原作者 / 上游 | 本仓库位置 | 许可证及附带声明 |
|---|---|---|---|
| **Tiny Invaders v3.1** | Daniel C（2018–2021），Tinyjoypad ATTINY85；上游 <https://www.tinyjoypad.com>，原始上传文件 `Tiny-invaders.ino`、`spritebank.h` | [`APP/functions/games/invaders/`](APP/functions/games/invaders/) | **GPL-3.0-or-later**；原版 128×64 标题／背景、24 格×10 关敌机、飞船、子弹图形和掩体数据逐字节保存；重写非阻塞时序、逻辑与只读渲染，移除 AVR/FastTinyDriver；完整 [`LICENSE-GPL-3.0`](APP/functions/games/invaders/LICENSE-GPL-3.0)。 |
| **Tiny Bomber** | Daniel C（2018–2021），<https://github.com/phoenixbozo/TinyJoypad>；原始文件为用户提供的 `tiny-bomber.ino`、`spritebank.h` | [`APP/functions/games/bomber/`](APP/functions/games/bomber/) | **GPL-3.0-or-later**；保留原版横屏像素素材、15×7 障碍位图、三关地图、四敌随机转向与原始爆炸规则，移植为非阻塞 C/MiaoUI；保留原作者声明及 [`LICENSE-GPL-3.0`](APP/functions/games/bomber/LICENSE-GPL-3.0)，不包含 AVR FastTinyDriver。 |
| **TinyPacman** | Daniel C（2018–2021），Tinyjoypad/ATtiny85；原版图形与碰撞资源见上传的 `spritebank.h`，项目 <https://github.com/phoenixbozo/TinyJoypad> | [`APP/functions/games/pacman/`](APP/functions/games/pacman/) | **GPL-3.0-or-later**；沿用原版像素素材、迷宫、6×7 碰撞及四幽灵随机运动，移植为非阻塞 C/MiaoUI。完整许可证 [`LICENSE-GPL-3.0`](APP/functions/games/pacman/LICENSE-GPL-3.0)；本仓库顶层 AGPL-3.0-only 并不替代第三方 GPL 版权声明。 |
| **Tiny Arkanoid (2025)** | Daniel Champagne (2018–2021)，Tinyjoypad/ATtiny85，<https://github.com/phoenixbozo/TinyJoypad>；基于用户提供的 `tiny-arkanoid.ino` 和 `spritebank.h` | [`APP/functions/games/arkanoid/`](APP/functions/games/arkanoid/) | **GPL-3.0-or-later**；保留五张原版关卡表及像素素材，适配 MiaoUI、SPI DMA 和 Beep Service。完整 [GPL 许可证](APP/functions/games/arkanoid/LICENSE-GPL-3.0)。 |
| **Tetris (ported from Tiny Tris v3, 2025)** | Daniel C（2019–2021），Tinyjoypad/ATtiny85，<https://github.com/phoenixbozo/TinyJoypad>；基于用户提供的 `tiny-tris_v3.ino` 和 `spritebank_TTRIS.h` | [`APP/functions/games/tetris/`](APP/functions/games/tetris/) | **GPL-3.0-or-later**；保留原版方块、画面和数字素材，使用 MiaoUI 和 Beep Service。完整 [GPL 许可证](APP/functions/games/tetris/LICENSE-GPL-3.0)。 |
| **MiaoUI** | JFeng-Z，<https://github.com/JFeng-Z/MiaoUI> | [`APP/MiaoUI/`](APP/MiaoUI/) | **MIT License**；保留 [`APP/MiaoUI/LICENSE`](APP/MiaoUI/LICENSE) 与源文件版权头。项目中进行了 MiaoUI 菜单、交互、帧调度与显示驱动等定制。 |
| **u8g2**（Universal 8bit Graphics Library） | olikraus，<https://github.com/olikraus/u8g2> | [`BSP/u8g2/`](BSP/u8g2/) | **BSD 2-Clause 风格许可**；版权及完整条款见 [`BSP/u8g2/u8g2.h`](BSP/u8g2/u8g2.h) 与对应源文件头。 |
| **Fusion Pixel Font**（缝合像素字体） | TakWolf，<https://github.com/TakWolf/fusion-pixel-font> | [`APP/MiaoUI/fonts/font_fusion_pixel_p12_zhcn.c`](APP/MiaoUI/fonts/font_fusion_pixel_p12_zhcn.c) 中的全部字形子集（比例版） | **SIL Open Font License 1.1**；见 [`LICENSE-OFL-FUSION`](APP/MiaoUI/fonts/LICENSE-OFL-FUSION)。 |
| **Ark Pixel Font**（方舟像素字体） | TakWolf，<https://github.com/TakWolf/ark-pixel-font> | 同一混合中文字库中的部分基础/补充字形 | **SIL Open Font License 1.1**；见 [`LICENSE-OFL-ARK`](APP/MiaoUI/fonts/LICENSE-OFL-ARK)。 |
| **Protothreads 1.4** | Adam Dunkels / Swedish Institute of Computer Science（SICS），<https://dunkels.com/adam/pt/> | [`Scheduler/pt1.4/`](Scheduler/pt1.4/) | **BSD 3-Clause 风格许可**；完整版权、条件及免责声明保留于 [`Scheduler/pt1.4/pt.h`](Scheduler/pt1.4/pt.h) 等上游头文件中。 |
| **STM32CubeG0 HAL/LL、CMSIS** | STMicroelectronics、Arm 及相应贡献者 | [`Core/`](Core/)、[`Drivers/`](Drivers/) | 各厂商组件的许可证以仓库内相应文件头及分发包声明为准，勿将其误认为项目原创源码。 |

**共享中文字库说明：** `font_fusion_pixel_p12_zhcn.c` 是由用户提供的 Fusion Pixel 12px Prop zh_Hans BDF（FONT_VERSION 2026.09.25）筛选并重新编码的 u8g2 RLE 字形子集，保留原始位图及字符度量。此前混合版的 MiaoUI、Fusion Pixel、Ark Pixel 许可声明保留；Fusion Pixel 上游组件 Cubic 11、Galmuri 的原始许可附在 `LICENSE-OFL-FUSION` 文件中。字体的再分发、修改和名称使用应符合原始许可证；本说明不赋予超出原许可证的权利。

**Protothreads 与 coroOS 的区别：** `Scheduler/pt1.4/` 为第三方的栈less Protothreads 实现；`Scheduler/coroOS.c/.h` 为本项目基于 Protothreads 的协程调度封装，不应将两者混同或抹去上游版权。

上述开源项目名称仅用于准确署名与来源说明，**不代表原作者为本项目提供保证、赞助或认可**。发布源码或二进制文件时应依相关许可证保留必要版权及许可文本。

## AI 辅助创作与责任说明

本项目在固件开发过程中使用了 **OpenAI ChatGPT** 协助代码实现、问题定位、调试思路和技术文档编写；同时使用 **DeepSeek v4.1f** 辅助调试和文档整理。相关工作涉及架构讨论、功能移植、修复建议、代码审阅以及技术说明，具体使用情况可能随版本演进而变化。

AI 提供的内容并不等同于完成编译、板级测试或功能验证。所有 AI 辅助生成的修改均需由项目维护者审查，并以实际工具链编译、示波器/仪表测量及硬件测试为准；最终工程决策、第三方许可合规、发布与安全责任仍由项目维护者承担。**本声明不将 AI 服务提供方列为固件版权持有人或第三方库的原作者。**
