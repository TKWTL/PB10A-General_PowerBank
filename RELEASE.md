# 发布流程与检查清单

对外发布（公开仓库、打包分发、交付）与本仓库日常开发是两件事。下面每一条都要在打 tag **之前**完成。

## 1. `相关数据手册/`：已入库，发布前必须从历史里移除

手册已提交进本仓库，让远程仓库的 agent 能直接读器件参数（24 份，约 36.6 MB，最大单文件 12.06 MB）。
版权属原厂，**不得随对外发布分发**，所以发布前必须从 git 历史里移除，而不是只删文件。

```powershell
# 1) 重写历史，把该目录从所有提交里抹掉
git filter-repo --path 相关数据手册 --invert-paths

# 2) 强推（历史被重写，必须 force）
git push origin --force --all
git push origin --force --tags
```

三点必须知道：

- **只 `git rm` + 提交是不够的**：PDF 仍在历史对象里，`git log --all -- 相关数据手册` 与
  `git cat-file -p <blob>` 依然取得到内容。
- **强推后服务器不会立刻清理不可达对象**，知道旧 SHA 的人仍可能把它取回来。本仓库访问量极低，
  这个残留可以接受，但它不等于"彻底消失"；真要清干净需要找 GitHub Support 清理缓存对象。
  另：公开仓库会被爬虫索引 default 分支，**PDF 在库期间是直接可下载的**，不依赖有人访问。
- 已经 clone / fork 过的副本不受重写影响。

兜底机制（不能替代上面的重写）：

| 机制 | 作用 |
|---|---|
| `.gitattributes` 里 `相关数据手册/** export-ignore` | `git archive` 打出的发布包不含该目录 |
| `.git/info/exclude`（本机，不入库） | 一旦用 `git rm --cached` 移除，后续 `git add -A` 不会又把它加回来 |

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
详见 [`Hardware/Manufacturing/README.md`](Hardware/Manufacturing/README.md)。

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
