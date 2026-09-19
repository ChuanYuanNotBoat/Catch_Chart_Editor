# Malody Catch Chart Editor / Malody Catch 谱面编辑器

A Qt 6 desktop chart editor for Malody Catch mode.
面向 Malody Catch 模式的 Qt 6 桌面谱面编辑器。

## Version status / 版本状态

- Current release / 当前版本：**Beta v1.11.2（2026-09-19）**
- Repository state / 仓库状态：**Released（2026-09-19）**
- Git tag / 标签：`v1.11.2`
- Download / 下载：[GitHub Releases](https://github.com/ChuanYuanNotBoat/Catch_Chart_Editor/releases/latest)

`docs/history.md` 中已经发布的版本段落视为冻结记录；当前未发布维护内容记录在文件顶部的 `Unreleased` 段。

## Documentation / 文档

- [文档总索引](docs/README.md)
- [用户帮助](docs/help.md)
- [版本状态与升级说明](docs/version.md)
- [完整更新历史](docs/history.md)
- [开发者项目指南](docs/AI_PROJECT_GUIDE.md)
- [工程优化与重构 TODO](docs/ENGINEERING_TODO.md)
- [未来产品与架构规划](docs/FUTURE_ROADMAP.md)
- [测试指南](TESTING.md)
- [插件 SDK](src/plugin/README.md)

## Current focus / 当前重点

- Beta v1.11.2 fixes shortcut capture, persistence, and conflict handling, adds Alt+Up/Down note-mode cycling, and adds `View -> Move View...` for relocating panels; it also carries the AutoTiming 2 based BPM measurement, explicit tempo-map application, and expanded background image import from the same maintenance line.
  Beta v1.11.2 修复快捷键录入、持久化与冲突处理，新增 Alt+↑/↓ 音符模式循环与 `View -> Move View...` 面板位置迁移；同维护线还包含 AutoTiming 2 BPM 测量、显式 tempo map 应用与背景图导入格式扩展。
- Export now flushes the working copy before packaging `.mcz`, restores `meta.offset` through the main audio Sound Note, and keeps OGG conversion and background import atomic.
  导出 `.mcz` 前先落盘工作副本，通过主音频 Sound Note 补回 `meta.offset`，OGG 转换与背景导入保持原子提交。
- Unified shortcut routing across canvas, Note Chain, and plugin tools remains a tracked TODO in [docs/ENGINEERING_TODO.md](docs/ENGINEERING_TODO.md); the settings dialog currently covers registered menu actions only.
  画布、Note Chain 与插件工具的快捷键统一路由仍是 [docs/ENGINEERING_TODO.md](docs/ENGINEERING_TODO.md) 中的 TODO；快捷键设置当前只覆盖已注册的菜单动作。

完整变更见 [docs/history.md](docs/history.md) 顶部，近期维护见 [docs/ENGINEERING_TODO.md](docs/ENGINEERING_TODO.md)，长期方向见 [docs/FUTURE_ROADMAP.md](docs/FUTURE_ROADMAP.md)。

## Features / 功能

- Load, create, edit, save, and export Malody Catch `.mc` / `.mcz` charts.
- Place, move, select, copy, paste, mirror, and delete Normal / Sound / Rain notes.
- BPM table editing, automatic BPM measurement, timeline navigation, and playback-speed control.
- Skin rendering, division colors, Hyperfruit hints, background images, note sounds, and realtime preview.
- Native curve-to-note workflow with per-segment density and curve/polyline shapes.
- V3 curve sidecar under `.mcce-plugin/*.curve_tbd.json`, with CAS revision checks and legacy data import.
- Native and JSON-lines process plugins, tool actions, floating panels, canvas overlays, and host batch edits.
- Configurable ADS workspace with a protected main editor, discoverable panel recovery, stable compact-tool sizing, and independent classic/multi-window layouts.
- Chinese, English, and Japanese UI translations.

## Build

### Requirements

- CMake 3.20+
- C++17 compiler
- Qt 6 components: Core, Widgets, Multimedia, Concurrent, LinguistTools, Test

Qt Advanced Docking System 5.1.1 is vendored in `third_party/QtAdvancedDockingSystem`.
AutoTimingCore is pinned as a Git submodule in `third_party/AutoTimingCore`.
Neither dependency is downloaded by CMake.

After cloning, initialize submodules once:

```powershell
git submodule update --init --recursive
```

### Windows / multi-config

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

Release:

```powershell
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

### Single-config generators

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Build output also receives the default skin, runtime plugins, documentation, note sounds when present, third-party license files, and the AutoTimingCore attribution notice.

## Quick start / 快速上手

1. Open or create a chart from the `File` menu.
2. Use the `Note Editor` panel to choose Note, Rain, Delete, Select, or Curve mode.
3. Use the mouse wheel to navigate; `Ctrl + wheel` changes the timeline scale.
4. Press `Space` to play/pause and use `Time Division` plus `Grid Snap` for precise placement.
5. Drag a panel header to dock or float it; the live outline shows the target region, while the chart workspace remains a protected central area. Reopen closed panels from the toolbar `Panels` menu. Classic and multi-window layouts are saved independently.
6. Save as `.mc` or export a Malody-compatible `.mcz` package.

Detailed controls are documented in [docs/help.md](docs/help.md).

## Repository map / 仓库结构

```text
src/app/                 application and main-window composition
src/controller/          chart, selection, and playback orchestration
src/editor/NoteChain/    native curve editor and V3 persistence
src/file/                chart/project/skin/plugin file handling
src/model/               chart data models
src/plugin/              plugin host and SDK
src/render/              canvas renderers
src/ui/                  panels, dialogs, and ChartCanvas
src/audio/               playback, note sounds, BPM and AutoTiming
tests/                   core and docking regression tests
docs/                    user and developer documentation
plugins/                 runtime plugins and SDK samples
third_party/             vendored dependencies and pinned submodules
```

## File compatibility / 文件兼容

The Malody `.mc` JSON structure is a compatibility boundary and must not be extended with editor-only fields. Editor-specific data belongs in sidecar files under `.mcce-plugin/`.

谱面 `.mc` 的 JSON 结构是兼容边界，不得写入编辑器私有字段；编辑器扩展数据统一保存到 `.mcce-plugin/` sidecar 文件。

See [docs/ARCHITECTURE_FORMAT_REFERENCE.md](docs/ARCHITECTURE_FORMAT_REFERENCE.md) for format details.

## License / 许可

The project is licensed under GPL-3.0; see [LICENSE](LICENSE).

Qt Advanced Docking System 5.1.1 is licensed under LGPL-2.1. Its source and license files are included in [third_party/QtAdvancedDockingSystem](third_party/QtAdvancedDockingSystem).

AutoTimingCore is pinned at `90f7a9529e1bcdb15475bfa9db5d873b0a38f1e2`.
Its Malody legacy source license is not confirmed; see
[docs/AUTOTIMING_VENDORING.md](docs/AUTOTIMING_VENDORING.md) and the submodule's
`ATTRIBUTION.md` before redistribution.

Special thanks to **myhome** for the included skin: [skin page](https://m.mugzone.net/store/skin/detail/5982).
