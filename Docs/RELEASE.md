# 发布流程与检查清单

对外发布（公开仓库、打包分发、交付）与本仓库日常开发是两件事。下面每一条都要在打 tag **之前**完成。

## 1. `相关数据手册/`：只留本机，不进版本库

器件手册版权属原厂，**只在本机保留**：`.git/info/exclude` 已排除它，`git add -A` 不会带进提交，
`git status` 里也看不到。目录规模固定（24 份，约 36.6 MB）。

**本仓库是公开仓库**（匿名可 clone），所以手册不能进 main 的历史：

- 已实测：匿名克隆只拿到 263 个跟踪文件，`相关数据手册/` 目录、以及历史里任何指向它的对象都不存在，
  克隆后的 `.git` 只有 1.37 MB。也就是说**别人 clone 不会同步到手册，也不会同步到任何手册历史**。
- 已推送的历史撤不回来：`git rm` 只是新增一个删除提交，PDF 仍在历史对象里；只能
  `git filter-repo` 重写并强推，而且 GitHub 侧对象可能仍可按 SHA 访问到。

需要让远程仓库的 agent 读器件参数时，用**独立的私有仓库**承载（例如 `TKWTL/PB10A-Datasheets`），
把手册推那里，本仓库 README 写一句指路即可。**不要在本仓库执行 `git add -f 相关数据手册`。**

三层机制，互不替代：

| 机制 | 作用 | 局限 |
|---|---|---|
| `.git/info/exclude`（本机，不入库） | 默认不会被 `git add -A` 带进提交 | 每个 clone 各自设置，不随仓库分发 |
| `.gitattributes` 里 `相关数据手册/** export-ignore` | `git archive` 发布包自动不含该目录 | 只影响 `git archive`，不影响 `git clone` |
| `git rm -r --cached` + 提交 | 后续提交不再包含 | **清不掉已推送的历史** |

万一哪天误推了：删除提交无效，必须 `git filter-repo --path 相关数据手册 --invert-paths` 重写历史、
强推，然后让所有克隆重新 clone。

## 2. 检查：`Images/`

- 实物照片与示波器截图里常带**工作台、屏幕、序列号、姓名、便签**，逐张看过再发。
- 需要保留但不该公开的图，移出仓库而不是靠 `export-ignore` 兜底。
- 目录在 `.gitattributes` 中是 `-text -diff`：入库即逐字节，不会被行尾转换改写。

## 3. 检查：`Hardware/`

- `Hardware/Preview/*.tel` 是**文本网表**，会进 diff——确认其中没有不该公开的网络名/位号信息。
- `Hardware/Preview/*.pdf`、`Hardware/source/*.epro2` 按 binary 管理，逐字节入库。
- Gerber / BOM / 结构件是否随发布，按项目性质决定；一旦公开即等于对外释放制造文件。

## 4. 检查：构建产物与本地状态

`.gitignore` 已把 Keil 产物限定在 `/Firmware/**` 下，发布前仍要确认：

- `Firmware/MDK-ARM/Objects/`、`Listings/` 不在索引里（`git ls-files | Select-String 'Objects/|Listings/'`）。
- `*.uvguix`、`*.mxproject` 未入库。
- `*.uvoptx` **是入库的**（存调试器与下载算法），但会带断点与本机路径——需要时清掉再提交。
- 量产固件 `*.hex`/`*.bin` 若放在 `Hardware/Manufacturing/`，现在**会被跟踪**（刻意如此），
  发布前确认是否要随包分发。

## 4.5 量产固件：手动拷入 + 提醒

固件二进制平时不入库，**只有在确认"这个版本可以发布"之后才手动拷进** `Hardware/Manufacturing/`：

```powershell
Copy-Item Firmware\MDK-ARM\Objects\PB10A-General_PowerBank.hex Hardware\Manufacturing\
git add Hardware/Manufacturing/PB10A-General_PowerBank.hex
git commit -m "firmware: PB10A-General_PowerBank v1.0.0 (tag v1.0.0)"
```

拷入后核对三处版本号一致：文件名、`Firmware/README.md` 的记录、外层 git tag。
忘记这一步的后果是发布包里没有固件——所以它被单独列为一步，而不是附在第 4 步里。
详见 [`../Hardware/Manufacturing/README.md`](../Hardware/Manufacturing/README.md)。

## 5. 检查：许可与第三方声明

- 根 `LICENSE` 与实际意图一致（当前 GNU AGPL-3.0-only）。
- 仓库内第三方代码/字体的授权与出处齐全；模板类仓库见 `Firmware/THIRD_PARTY_NOTICES.md`。

## 6. 打发布包

```powershell
# export-ignore 生效，发布包自动剔除 相关数据手册/
git archive --format=zip -o PB10A-v1.0.0.zip HEAD
# 核对：不应出现 相关数据手册
tar -tf PB10A-v1.0.0.zip | Select-String '相关数据手册'
```

公开发布前，`git ls-files 相关数据手册` 应返回空。
