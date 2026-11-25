# Sentinel Defense Grid v2.3.1

**KIMI FUSION EDITION** — 原生 Windows 全屏屏保，以黑绿终端墙呈现高强度入侵与边界防御。

v2.3.1 在 v2.3.0 的六类高密度界面与高速「边界拦截」之上，增加 8 首轮换配乐和可配置的 Windows 屏保空闲等待。14 个阶段、3 幕、7 种布局、6 类轮换事件保持不变；轨道截停、证据链、防御矩阵和最后信号均由 C++ 实时绘制，不使用网页、游戏素材或外部图片。

程序不执行真实攻击或终端命令，不联网，不加密或删除真实文件，也不需要管理员权限。

## 快速开始

- 双击 `Sentinel-v2.3.1.exe`：全屏运行。
- 调试运行：`Sentinel-v2.3.1.exe --windowed`。
- 退出：按 `Enter`，输入默认覆盖码 `1984`，再按 `Enter`；或使用 `Ctrl+Shift+Q`。
- `.scr` 与 exe 内容相同，建议通过安装脚本注册为屏保。

### 安装为 Windows 屏保

运行发布包中的 `install.bat`。脚本会把程序、配置、引文库、音乐与依赖复制到 `%LOCALAPPDATA%\Sentinel\`，按 `config.ini` 的 `IdleMinutes` 设置当前用户的 `SCRNSAVE.EXE` 与等待时间，然后打开 Windows 屏保设置。

`IdleMinutes=0`（默认）表示只在双击时运行，安装脚本会关闭 Windows 自动启动；`5`、`10`、`15`、`60`、`120`、`180` 分别表示 5 分钟、10 分钟、15 分钟、1 小时、2 小时、3 小时无操作后启动；也可以填写 `1..1440` 的任意分钟数。修改后重新运行 `install.bat` 让 Windows 读取新值。安装后实际运行读取 `%LOCALAPPDATA%\Sentinel\config.ini`，修改该文件后同样重新运行 `install.bat`。

不需要管理员权限，不修改 `.scr` 文件关联。卸载运行 `uninstall.bat`。

> 只有安装脚本会修改当前用户的屏保注册表项；屏保程序本身不写注册表。安装测试完成后已恢复测试前的屏保设置。

## 操作

| 输入 | 行为 |
| --- | --- |
| `Enter` | 打开收容控制；窗口打开时提交覆盖码 |
| `Ctrl+Enter` | 从聚焦终端直接打开收容控制 |
| `Esc` | 关闭收容窗口或退出终端焦点 |
| `M` / `F8` | 切换下一首背景音乐 |
| 点击底部 `[M / F8] NEXT TRACK` | 切换下一首背景音乐 |
| `F1` | 显示 / 隐藏调试信息 |
| `F5` | 重读配置、音乐、提示音和引文 |
| `F6` | 依次预览七种布局，不改写配置 |
| `F7` | 切换事件画像 |
| `Ctrl+Shift+Q` | 紧急退出 |
| 鼠标点击终端 | 聚焦、切页、展开文件树或输入 |

覆盖码支持 4–32 位可打印 ASCII，区分大小写；非法配置回退到 `1984`。连续输错三次只锁定输入五秒，倒计时结束仍可使用覆盖码。

**输入保护：** 收容窗口或终端正在输入时，`F8`、`M` 和底部点击都不会切歌；`M` 保留为普通输入字符。三个入口共用第二层保护，避免后续新增调用路径绕过。

## v2.3.1 更新

- 音乐库扩展为 8 首曲目，仍按文件名顺序轮换，底部隐藏角落点击切歌；`Music=off` 可关闭。
- 配置新增 `IdleMinutes`：`0`（默认）仅双击运行；`5/10/15/60/120/180` 或任意 `1..1440` 分钟无操作后由 Windows 启动屏保。修改后重新运行 `install.bat`。
- `install.bat` 现在按 `IdleMinutes` 写入 `ScreenSaveActive` / `ScreenSaveTimeOut`，不再固定 300 秒；`IdleMinutes=0` 会关闭 Windows 自动启动。
- 其余 v2.3.0 的 14 阶段、7 布局、边界拦截、音频、引文和覆盖码流程不变。

## v2.3.0 融合升级

- **7 场景 / 14 阶段：** ACT II 前段加入 Phase 05 `BOUNDARY SCAN` 与 Phase 06 `INTERCEPT STORM`，其余阶段、幕归属和时间轴同步扩展。
- **高速边界截停：** 14 个节点框沿内外双轨运动，文字保持水平；约 0.58 秒选择一次目标，依次经历锁定、截断、消灭、消散与签名再生。
- **动态核心：** 9×19 字符矩阵、旋转字符环、中央计数和校验码持续变化；左侧大计数与上行命令流加速，右侧使用独立清除事件流。
- **固定战略拓扑：** Tactical Signal Map 使用 14 节点、18 路由的固定布局，与高速轨道场景明确分工。
- **常驻信息骨架：** Split Ops 增加六层防御校验矩阵、入侵路径梯与证据带；Terminal 增加三个永久任务槽，间歇弹窗继续保留。
- **Last Signal：** 大标题、动态信号图、三张事件卡、六阶段处置链和高密度侧栏组成完整画面。
- **六类事件轮换：** 数据外泄、勒索加密、凭据转储、算力劫持、长期驻留、可用性破坏；每轮稳定，下一轮不重复，标题、英文副标题、技术标签、波形和指标一起变化。
- **文案统一：** 可见界面使用防御状态、追踪编号和处置指令，不再显示旧版出戏提示或已删除的边界标语。
- **低动效：** 冻结快速轨道与碎裂运动，保留目标状态、计数和高速数据更新。

## 配置

优先读取 exe 同目录的 `config.ini`，否则读取 `%APPDATA%\Sentinel\config.ini`。修改后按 `F5` 或重启生效。

```ini
[general]
Code=1984
Theme=auto             ; auto|terminal|split|boundary|warroom|forensic|containment|blackbox
Fps=30                 ; 10–60
Quality=auto
MonitorMode=all        ; all|primary
AutoPromptSeconds=0
IdleMinutes=0          ; 0=仅双击；1..1440 分钟无操作后由 Windows 启动
ShowSimulationTag=0    ; 兼容旧配置，界面始终使用防御状态栏
KeepDisplayOn=0
Language=bi
Speed=1.6              ; 0.5–3.0

[effects]
Sound=on
Flash=med              ; low|med|high
Shake=0                ; 保留兼容，始终无屏幕抖动
Glitch=1
ErrorText=1
Dust=med               ; off|low|med|high
Corruption=med         ; low|med|high
Motion=full            ; full|reduced
RainDensity=0.6        ; 已弃用

[quotes]
Quotes=on
QuotesFile=quotes.xlsx

[music]
Music=on
MusicDir=music
```

`music/` 按文件名顺序扫描 `.mp3`、`.wav`、`.m4a`，默认音量 32%。本包内置 8 首曲目：`A sudden blow.mp3`、`A sudden blow (v2).mp3`、`The wheels of the world.mp3`、`The wheels of the world 2.59.mp3`、`The ghost in the machine v2.mp3`、`A wilderness of mirrors.mp3`、`Slouching towards Bethlehem.mp3`、`Slouching towards Bethlehem V2.mp3`。

读取 `.xlsx` 引文需要同目录的 `zlib1.dll`；文件缺失或格式不支持时使用内置短引文。`Quotes=off`、`Music=off`、`Sound=off` 可分别关闭对应层。

## 从源码构建

需要 Visual Studio 2022 Build Tools（MSVC x64）和 Windows 10/11 SDK：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build.ps1
```

构建使用 C++17、静态 CRT、`/O2 /GL /MT`，生成 exe / scr，运行自检、性能基准和内容检查；任一检查失败都会中止打包。产物为 `dist\Sentinel-v2.3.1.zip`。

### 独立验证

```powershell
.\dist\Sentinel-v2.3.1.exe --selftest
.\dist\Sentinel-v2.3.1.exe --benchmark
.\dist\Sentinel-v2.3.1.exe --contentcheck
.\dist\Sentinel-v2.3.1.exe --musiccheck
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify-runtime.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify-window-messages.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\verify-input.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\audit-release.ps1
```

`--musiccheck` 通过真实 MFPlay 初始化和生产消息路径，检查两首曲目的 F8 / M / 底部点击往返、元数据同步，以及收容窗口与终端焦点下三个入口的保护。测试会短暂初始化音频。

截图与完整实图验收：

```powershell
.\dist\Sentinel-v2.3.1.exe --screenshot .\screenshots\boundary.png --theme boundary --scenario breach --at 16 --seed 12345
.\dist\Sentinel-v2.3.1.exe --screenshot .\screenshots\last.png --theme blackbox --scenario recovery --goal ransomware --at 16
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\capture-v230.ps1
```

截图模式不初始化音乐，所以底部显示 `NO ACTIVE TRACK` 属于截图模式的静音行为。

## 验证报告

发布包附带本次构建的原始报告：

- `selftest.txt`：30 分钟状态推进、14 阶段、六类事件、边界生命周期、低动效、七场景渲染、收容码与 GDI 边界。
- `benchmark.txt`：600 帧、2560×1440 离屏渲染、CPU、内存、GDI 与 exe 大小。
- `contentcheck.txt`：360 秒内容流与精确重复率。
- `musiccheck.txt`：真实音乐初始化及三个切歌入口回归。
- `config-smoke.txt`：全静音与低动效运行检查。
- `message-smoke.txt` / `input-smoke.txt`：窗口消息与 native SendInput 退出路径。
- `verification/package-audit.txt`：20 文件清单、版本、包内外哈希和残留进程审计。

窗口消息和自动输入验证不能替代所有人工测试。物理按键、全局热键、多显示器、不同音频设备及长期真实屏保驻留仍应在目标机复核。

## 技术结构

- **运行时：** C++17 / Win32 / GDI / msimg32，无 Electron、WebView 或 Python 运行时。
- **渲染：** 1280×720 虚拟坐标，内部缩放、32 位 DIB 双缓冲和最终拉伸。
- **状态：** 固定规模节点、14 阶段导演、3 幕、7 主题、有界日志与边界队列。
- **动效：** `motion.*` 管理阶段与主题计时；`tracefx.*` 提供确定性视觉原语；`boundary.cpp` 独立绘制轨道截停。
- **音频：** `winmm` 内存 WAV 提示音；MFPlay 背景音乐。
- **引文：** ZIP 本地头解析与 `zlib1.dll` 动态 inflate，支持 `.xlsx` / `.txt`。

```text
project/
  src/            原生源码、资源与清单
  music/          八首内置曲目
  quotes.xlsx     便携引文库
  tools/          构建、安装、截图、验证与发布审计
  screenshots/    全场景、六类事件、弹窗和低动效截图
  verification/   原始验证记录与最终汇总
  dist/           可运行程序与完整发布包
```
