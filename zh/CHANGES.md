# Sentinel Defense Grid v2.3.1

基线：v2.3.0 FUSION EDITION（14 阶段 / 3 幕 / 7 布局 / 边界拦截）。
本版只做增量，不改动 v2.3.0 的场景导演、视觉与退出流程。

## v2.3.1 更新

- 音乐库扩展为 8 首曲目：`A sudden blow.mp3`、`A sudden blow (v2).mp3`、`The wheels of the world.mp3`、`The wheels of the world 2.59.mp3`、`The ghost in the machine v2.mp3`、`A wilderness of mirrors.mp3`、`Slouching towards Bethlehem.mp3`、`Slouching towards Bethlehem V2.mp3`；仍按文件名排序、MFPlay 轮换，底部隐藏角落可切歌。
- `config.ini` 新增 `[general] IdleMinutes`：
  - `0`（默认）：双击才打开，安装脚本关闭 Windows 自动屏保。
  - `5`、`10`、`15`、`60`、`120`、`180`：5/10/15 分钟、1/2/3 小时无操作后由 Windows 启动。
  - `1..1440`：任意自定义分钟数。
- `install.ps1` 读取 `IdleMinutes`，写入当前用户 `ScreenSaveActive` / `ScreenSaveTimeOut`，不再固定 300 秒；修改后重新运行 `install.bat`。
- 版本资源、README、构建与安装脚本、包名统一到 v2.3.1。

## 保持不变

- 双击 `Sentinel-v2.3.1.exe` 立即全屏；`Enter` → 覆盖码（默认 `1984`）→ `Enter` 退出；`Ctrl+Shift+Q` 应急退出。
- 不联网、不执行真实攻击、不删除真实文件、不需要管理员权限；只有安装脚本修改当前用户屏保设置。
- `Sound` / `Quotes` / `Music` / `Theme` / `Speed` / `Code` 等配置继续有效。
