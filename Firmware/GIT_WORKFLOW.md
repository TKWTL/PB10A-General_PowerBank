# Git 工作流：Keil 工程文件与远程同步

本文件只解决一件事：**`.uvprojx` / `.uvoptx` 在远程同步时的冲突**。这是 IH01A 项目踩过的坑，
结论按"文件承载什么内容"分开处理，不要一刀切。

## 一次性设置（每个 clone 做一次）

```powershell
git config --local include.path ../.gitconfig
# 校验（应回显 true）
git config --get merge.keil-uvoptx.driver
```

不做这一步，uvoptx 冲突会退化成人工解 XML；做了之后自动保留本地。

## 三类文件的策略

| 文件 | 承载内容 | 是否入库 | 冲突策略 |
|---|---|---|---|
| `*.uvprojx` | 源文件列表、分组、IncludePath、宏定义、链接配置 | 是 | **人工合并**（承载真内容，自动化会丢东西） |
| `*.uvoptx` | 调试器选择、Flash 下载算法、断点、窗口状态 | 是 | **保留本地**（`merge=keil-uvoptx` 驱动自动完成） |
| `*.uvguix*`、`*.mxproject` | 纯窗口布局、CubeMX 元数据 | 否（`.gitignore`） | 不涉及 |

`uvoptx` 之所以不干脆忽略：它存着 CMSIS-DAP 调试器选择与 `STM32G0xx_128.FLM` 下载算法，
换机器或重新 clone 后丢失，每次都要手动重设。代价是它永远在变，所以用驱动兜住冲突。

## 冲突处理流程

### 1. 先看冲突落在哪个文件

```powershell
git status --short
```

### 2. `uvoptx` 冲突

配好驱动后**不会再出现**冲突标记。若还是出现了，说明本机没引入 `.gitconfig`：

```powershell
git config --local include.path ../.gitconfig
git checkout --ours -- Firmware/MDK-ARM/PB10A-General_PowerBank.uvoptx
git add Firmware/MDK-ARM/PB10A-General_PowerBank.uvoptx
```

> **rebase 时 ours/theirs 是反的**：`git rebase` 重放提交时，`--ours` 是上游分支（你 rebase 到的地方），
> `--theirs` 是你正在重放的提交。想"保留我本机的调试状态"时，在 rebase 里要用 `--theirs`。
> 拿不准就先 `git rebase --abort`，改用 merge，或者直接 `git checkout <自己分支> -- <文件>`。

### 3. `uvprojx` 冲突

它承载文件列表与编译配置，**必须看内容**：

```powershell
# 1) 看清双方各自改了什么（XML 一行一字段，diff 可读）
git diff --stat
git diff -- Firmware/MDK-ARM/PB10A-General_PowerBank.uvprojx

# 2) 冲突点集中在三处，逐处确认保留哪边
#    <Groups>          —— 是否新增/删除源文件或分组
#    <IncludePath>     —— 是否新增头文件目录
#    <Define>          —— 宏定义
#    另有 OutputDirectory / OutputName / ScatterFile / Device / PackID

# 3) 手边最稳的做法：取任意一侧整份，再用 Keil 把缺的文件加回去
git checkout --ours  -- Firmware/MDK-ARM/PB10A-General_PowerBank.uvprojx
# 在 Keil 里把对方新增的 .c/.h 加进对应分组、补 IncludePath，保存
git add Firmware/MDK-ARM/PB10A-General_PowerBank.uvprojx
```

合并完成后**必须用 Keil 打开一次并 Build**：uvprojx 是 XML，人工删错一个标签不会报错，
只会表现为"某个文件不参与编译"。

### 4. 提交前自检

```powershell
git status --short                   # 不应再有 UU / AA
git diff --cached --stat             # 确认改的是你想要的
git ls-files | Select-String 'Objects/|Listings/'   # 应为空
```

## 减少冲突的纪律

- **切换分支 / 换机器前先提交**。带着未提交的 uvprojx 走 rebase，冲突几乎必然。
- **uvprojx 的改动集中由一人提交**：加文件、改宏这类操作不要两人同时做。
- 改完立刻提交，别把 uvprojx 挂着过夜。
- 不要手工编辑 `Objects/`、`Listings/` 里的任何东西（本来也不入库）。
- 拉取用 `git pull --rebase` 还是 `git pull --no-rebase` 由你定，但**固定一种**；
  两种混用会让 uvoptx 的状态反复横跳。

## 常见症状对照

| 症状 | 原因 | 处理 |
|---|---|---|
| uvprojx 显示整文件 diff，但只改了一个文件 | 行尾被转成 CRLF | `.gitattributes` 已钉 `eol=lf`；`git add --renormalize .` 重来一次 |
| 每次都提示 `InvalidFlash` / `NotGenerated` 变化 | Keil 的构建状态字段 | 随它提交，无副作用 |
| 断点位置莫名漂移 | uvoptx 里存的是行号 | 正常现象，重设断点即可 |
| 换机器后下载算法为空 | uvoptx 没同步过来 | `git checkout -- .` 或从远端取 uvoptx；之后就会一直带着 |
| 合并后某个 .c 不参与编译 | uvprojx 手工合并漏了 `<File>` 项 | Keil 里重新加该文件，或 `git checkout` 回退重来 |
