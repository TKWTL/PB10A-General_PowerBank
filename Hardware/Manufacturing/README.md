# Hardware/Manufacturing · 制造文件与量产固件

| 内容 | 入库时机 |
|---|---|
| Gerber / 钻孔 / 拼板 / 工艺说明 | 打样或量产确认后 |
| 交互式 BOM（`BOM/*.html`） | 与 BOM 版本同批 |
| 量产固件 `*.hex` / `*.bin` | **仅在确认可以发布时手动拷入**，见下 |

## 量产固件：手动拷入 + 提醒

- 平时这个目录**不放**固件二进制。固件是 `Firmware/MDK-ARM/Objects/` 的构建产物，不入库。
- 只有在你确认"这个版本可以发布"之后，才把对应的 `*.hex` / `*.bin` 手动拷进来并提交。
- 提交信息写清版本，例如：
  `firmware: PB10A-General_PowerBank v1.0.0 (tag v1.0.0, 2026-01-20)`
- 拷入后核对三处版本号一致：文件名、`Firmware/README.md` 的记录、外层 git tag。
- `.gitignore` 刻意**不**忽略 `Hardware/**` 下的 `*.hex`/`*.bin`，所以手动拷入即可入库；
  反过来，忘记拷入时发布包里就是没有固件的——`Docs/RELEASE.md` 的清单为此留了一步。
