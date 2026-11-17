# Hardware

硬件资料：原理图、PCB、BOM、结构件与制造文件。

| 目录/文件 | 内容 |
|---|---|
| `Schematic/` | 原理图源文件与导出的 PDF |
| `PCB/` | 版图源文件、Gerber、叠层与工艺说明 |
| `BOM/` | 物料清单（含位号、封装、替代料） |
| `Mechanical/` | 结构件、外壳、3D 模型（STEP/STL） |
| `Interface/` | 接插件定义、测试点、跳线说明 |

约定：

- 二进制工程文件（`.epro2`、`.step`、`.dxf`）在 `.gitattributes` 里按 binary 处理，不做文本 diff。
- 每次打样必须留一份与固件版本对应的 tag 说明（`rev`、日期、改动点）。
- 未验证的设计不要写成"已验证"；当前目录为空表示硬件资料尚未入库。
