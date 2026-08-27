# Malody Catch Chart Editor / Malody Catch 谱面编辑器

A Qt 6 desktop chart editor for Malody Catch mode.
面向 Malody Catch 模式的 Qt 6 桌面谱面编辑器。

## Version status / 版本状态

- Current release / 当前版本：**Beta v1.11.0（2026-08-27）**
- Git tag / 标签：`v1.11.0-beta`
- Download / 下载：[GitHub Releases](https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor/releases/latest)

`docs/history.md` 中已经发布的版本段落视为冻结记录。Beta v1.11.0 的变更记录位于文件顶部，不回填 Beta v1.10.5。

## Documentation / 文档

- [文档总索引](docs/README.md)
- [用户帮助](docs/help.md)
- [版本状态与升级说明](docs/version.md)
- [完整更新历史](docs/history.md)
- [开发者项目指南](docs/AI_PROJECT_GUIDE.md)
- [测试指南](TESTING.md)
- [插件 SDK](src/plugin/README.md)

## Beta v1.11.0 focus / 本版本重点

- Native C++ Note Chain editor with direct canvas interaction, curve sampling, note snapping, unified undo/redo, and V3 sidecar compatibility.
  原生 C++ 曲线编辑器：画布直操、曲线采样、音符吸附、统一撤销/重做和 V3 sidecar 兼容。
- Composable ADS workspace: panels can dock, split, tab, float, persist, and restore like modern creative tools.
  ADS 可组合工作区：面板支持停靠、拆分、标签组合、浮动及布局持久化。
- Smooth native floating windows with themed Windows title bars and non-blocking first-frame layout.
  浮动窗口原生标题栏跟随主题，并使用非阻塞首帧布局以减少吸附和创建卡顿。
- Updated architecture, format, AutoTiming, Note Chain, testing, and plugin documentation.
  更新架构、格式、AutoTiming、曲线编辑器、测试及插件文档。

完整变更见 [docs/history.md](docs/history.md) 顶部的 Beta v1.11.0 段落。

## Features / 功能

- Load, create, edit, save, and export Malody Catch `.mc` / `.mcz` charts.
- Place, move, select, copy, paste, mirror, and delete Normal / Sound / Rain notes.
- BPM table editing, automatic BPM measurement, timeline navigation, and playback-speed control.
- Skin rendering, division colors, Hyperfruit hints, background images, note sounds, and realtime preview.
- Native curve-to-note workflow with per-segment density and curve/polyline shapes.
- V3 curve sidecar under `.mcce-plugin/*.curve_tbd.json`, with CAS revision checks and legacy data import.
- Native and JSON-lines process plugins, tool actions, floating panels, canvas overlays, and host batch edits.
- Configurable ADS workspace with persistent dock/floating layout.
- Chinese, English, and Japanese UI translations.

## Build

### Requirements

- CMake 3.16+
- C++17 compiler
- Qt 6 components: Core, Widgets, Multimedia, LinguistTools, Test

Qt Advanced Docking System 5.1.1 is vendored in `third_party/QtAdvancedDockingSystem`; building does not download it from the network.

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

Build output also receives the default skin, runtime plugins, documentation, note sounds when present, and ADS license files.

## Quick start / 快速上手

1. Open or create a chart from the `File` menu.
2. Use the `Note Editor` panel to choose Note, Rain, Delete, Select, or Curve mode.
3. Use the mouse wheel to navigate; `Ctrl + wheel` changes the timeline scale.
4. Press `Space` to play/pause and use `Time Division` plus `Grid Snap` for precise placement.
5. Drag panel tabs to dock, split, combine, or float them; reset from `View -> Panels -> Reset Panel Layout`.
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
third_party/             vendored dependencies and their licenses
```

## File compatibility / 文件兼容

The Malody `.mc` JSON structure is a compatibility boundary and must not be extended with editor-only fields. Editor-specific data belongs in sidecar files under `.mcce-plugin/`.

谱面 `.mc` 的 JSON 结构是兼容边界，不得写入编辑器私有字段；编辑器扩展数据统一保存到 `.mcce-plugin/` sidecar 文件。

See [docs/ARCHITECTURE_FORMAT_REFERENCE.md](docs/ARCHITECTURE_FORMAT_REFERENCE.md) for format details.

## License / 许可

The project is licensed under GPL-3.0; see [LICENSE](LICENSE).

Qt Advanced Docking System 5.1.1 is licensed under LGPL-2.1. Its source and license files are included in [third_party/QtAdvancedDockingSystem](third_party/QtAdvancedDockingSystem).

Special thanks to **myhome** for the included skin: [skin page](https://m.mugzone.net/store/skin/detail/5982).
