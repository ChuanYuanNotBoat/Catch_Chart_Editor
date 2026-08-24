# Malody Catch Editor 开发者项目指南

> 面向新开发者和代码代理的当前仓库速查。
> 当前开发周期：**Beta v1.11.0（开发中）**
> 最新发布：**Beta v1.10.5**
> 最后核对：2026-08-24

## 1. 项目边界

Malody Catch Editor 是 Qt 6 / C++17 桌面谱面编辑器，主目标是编辑 Malody Catch `.mc` 谱面并导入/导出 `.mcz`。

核心约束：

- 项目当前只维护桌面链路；旧 Android/QML 移动端方案已经移除。
- `.mc` JSON 必须保持 Malody 兼容，编辑器扩展数据只能写入 sidecar。
- 当前曲线编辑器是内部 C++ 模块，不是 Python 插件。
- 面板系统使用 vendored Qt Advanced Docking System 5.1.1。
- 用户已有工作区可能包含未提交修改；修改前必须检查 Git 状态并保留无关变更。

## 2. 技术栈与目标

| 项目 | 当前值 |
|------|--------|
| 语言 | C++17 |
| UI | Qt 6 Widgets |
| 音频 | Qt 6 Multimedia |
| 构建 | CMake 3.16+ |
| 面板 | Qt Advanced Docking System 5.1.1（静态 vendored） |
| 翻译 | Qt Linguist，`resources/translations/*.ts` |
| 测试 | CTest + 两个独立测试可执行程序 |
| 桌面平台 | Windows 为主要验证平台；代码保留 macOS/Linux 支持 |

主目标：

- `CatchChartEditor`：桌面主程序；
- `CatchChartEditorTests`：核心模型、I/O、控制器和 Note Chain 测试；
- `DockingLayoutTests`：ADS 布局、浮动窗口和原生主题测试。

## 3. 构建与测试

Windows 多配置构建：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure

cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

常用单目标：

```powershell
cmake --build build --config Debug --target CatchChartEditorTests --parallel
cmake --build build --config Debug --target DockingLayoutTests --parallel
cmake --build build --config Debug --target CatchChartEditor --parallel
```

完整测试策略见 [../TESTING.md](../TESTING.md)。

## 4. 源码索引

| 路径 | 责任 |
|------|------|
| `src/main.cpp` | Qt 应用入口与运行时版本字符串 |
| `src/app/` | Application、MainWindow、菜单、对话框、主题和工作区装配 |
| `src/model/` | Note、BPM、MetaData、Chart、Skin 数据模型 |
| `src/controller/` | Chart、Selection、Playback 的业务编排与信号 |
| `src/ui/` | 编辑面板、对话框、时间线、预览和 ChartCanvas |
| `src/ui/CustomWidgets/ChartCanvas/` | 画布输入、播放、粘贴、渲染和 Note Chain 接线 |
| `src/editor/NoteChain/` | 原生曲线状态、采样、交互与 V3 sidecar |
| `src/render/` | Note、网格、背景、分度颜色和 Hyperfruit 渲染 |
| `src/file/` | `.mc` / `.mcz`、工作副本、皮肤、插件加载和文件注册表 |
| `src/audio/` | 音频、音效、BPM 测量和 AutoTiming |
| `src/plugin/` | 插件接口、管理器和外部进程适配器 |
| `src/utils/` | 时间数学、设置、日志、诊断、主题和哈希 |
| `resources/` | 翻译、默认皮肤、图标和运行资源 |
| `plugins/` | 运行时插件、同步脚本和样例 |
| `tests/` | 核心与 ADS UI 回归测试 |
| `third_party/QtAdvancedDockingSystem/` | vendored ADS 源码及 LGPL 文件 |

## 5. 运行时数据流

```text
MainWindow
  ├─ ChartController ── Chart ── ChartIO / ProjectIO
  ├─ SelectionController
  ├─ PlaybackController ── AudioPlayer
  ├─ ChartCanvas ── render/*
  │    └─ NoteChainEditor ── NoteChainState / Persistence
  ├─ CDockManager ── Navigation / Preview / Note / BPM / Meta / plugin panels
  └─ PluginManager ── native plugins / ExternalProcessPlugin
```

约束：

- 数据修改优先经 Controller 完成，避免 UI 直接产生无法撤销的模型变更。
- `ChartController` 已把粗粒度 `chartChanged` 拆分为 notes/BPM/meta 信号；新增监听时选最小范围。
- 播放态视觉刷新由 `PlaybackController` 帧信号驱动，不要再增加独立高频定时器。
- 工作副本和源文件同步由 `MainWindow`、`ProjectIO`、`ChartFileSystem` 协作；不要绕过该链路直接保存 sidecar。

## 6. 可组合工作区

`MainWindow::createCentralArea()` 创建一个 ADS `CDockManager`：

- `Chart Workspace` 是不可关闭的中央 Dock；
- `Navigation` 默认位于左侧；
- `Realtime Preview`、`Note Editor`、`BPM & Timing`、`Metadata` 可停靠、拆分、标签组合和浮动；
- 长编辑面板使用 `ForceScrollArea`，不得让内容最小高度传到主窗口；
- 布局通过 `Settings::dockLayoutState` 保存，`View -> Panels -> Reset Panel Layout` 恢复默认值；
- 插件 panel 也必须进入 ADS，不应另建固定右侧堆叠布局。

性能注意：

- 不在浮动/吸附路径中调用 `QApplication::processEvents()`、同步 `repaint()` 或为取句柄提前调用 `winId()`；
- Windows 原生标题栏主题由 `NativeWindowTheme` 在 Show/WinIdChange 后应用；
- 浮动预览交接必须保存全局屏幕坐标，并在正常事件循环中异步提交首次布局。

## 7. 原生 Note Chain

权威实现位于 `src/editor/NoteChain/`，由 `ChartCanvas` 直接输入和 `QPainter` 绘制。

- 状态使用 chart-space：laneX `0..512` + beat；
- `CanvasProjection` 负责 canvas/chart 转换；
- V3 sidecar 位于 `.mcce-plugin/{chartStem}.curve_tbd.json`；
- 保存采用 revision CAS 和 `QSaveFile` 原子提交；
- Python legacy anchors/handles/links 只在兼容读取时处理；
- 曲线历史通过 checkpoint 接入宿主统一 Undo/Redo；
- 旧 `builtin.note_chain_assist` 会被 `PluginManager` 跳过，不启动 Python。

详细约束见 [NOTE_CHAIN_EDITOR.md](NOTE_CHAIN_EDITOR.md)。

## 8. 文件格式与兼容性

### `.mc`

- JSON 根对象包含 `meta`、`note`、`time` 等 Malody 约定字段；
- 不得加入编辑器私有字段；
- beat 使用 `[bar, numerator, denominator]` triplet；
- 写回必须保留 Malody V 兼容字段和资源文件名语义。

### `.mcz`

- 由 `ProjectIO` 负责导入/导出；
- `ChartFileSystem` 注册允许同步/打包的谱面、音频、图片、视频与 sidecar 类型；
- 导出结构和路径规则必须用真实 Malody 包回归验证。

### sidecar

- 编辑器私有数据位于歌曲目录的 `.mcce-plugin/`；
- 当前类型包括 `curve_tbd.json`、`bpm_excludes.json`、`song_bpm.json`；
- sidecar 必须随工作副本、复制、保存和难度切换同步。

完整格式说明见 [ARCHITECTURE_FORMAT_REFERENCE.md](ARCHITECTURE_FORMAT_REFERENCE.md)。

## 9. 插件系统

支持两种插件：

| 类型 | 载体 | 调用方式 |
|------|------|----------|
| Native | `.dll/.so/.dylib` | `PluginInterface` C 导出和函数调用 |
| Process | `*.plugin.json` + executable/script | stdin/stdout JSON-lines |

Host API 当前为 v3，扩展点包括 tool actions、floating panels、canvas overlays/interactions、panel workspace 和 host batch edit。

- 运行目录固定为 `<appDir>/plugins`；
- `plugins/samples/` 不作为默认插件扫描入口；
- `builtin.note_color_formatter` 是当前内置进程插件；
- `builtin.note_chain_assist` 源码仅保留 legacy 兼容参考，宿主明确跳过；
- 新插件从 [../src/plugin/README.md](../src/plugin/README.md) 和 `plugins/samples/` 开始。

## 10. 文档与版本规则

- 当前开发版本是 Beta v1.11.0，最新发布仍是 Beta v1.10.5。
- 进行中变更只进入 `history.md` 顶部的 v1.11.0 段。
- 已发布版本段落冻结，不把后续工作追加到 v1.10.5。
- 临时审计、分支状态和迁移 TODO 不作为长期文档提交。
- 新增或删除文档时同步 [README.md](README.md) 总索引。

## 11. 修改后的最小验证

| 改动 | 最小验证 |
|------|----------|
| 模型、I/O、Controller、Note Chain | `core_minimal_tests` |
| ADS、面板、原生窗口主题 | `ui_docking_layout_tests` |
| CMake、依赖、资源部署 | Debug + Release 主程序构建 |
| 用户交互、音频、拖放、主题 | 对应手工回归 + 自动化测试 |
| 翻译可见字符串 | 更新 `.ts` 并至少检查 zh_CN/en_US/ja_JP |
