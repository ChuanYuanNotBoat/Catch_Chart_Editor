# Malody Catch Chart Editor / Malody Catch 谱面编辑器

A Qt 6 desktop chart editor for Malody Catch mode.
面向 Malody Catch 模式的 Qt 6 桌面谱面编辑器。

## Version status / 版本状态

- Current version / 当前版本：**Beta v1.12.0（2026-10-25）**
- Previous release / 上一版本：**Beta v1.11.2（2026-09-19）**
- Repository state / 仓库状态：**Released（2026-10-25）**
- Git tag / 标签：`v1.12.0`
- Download / 下载：[GitHub Releases](https://github.com/ChuanYuanNotBoat/Catch_Chart_Editor/releases/latest)

`docs/history.md` 中已经发布的版本段落视为冻结记录；后续未发布内容单独记录在新的 `Unreleased` 段。

## Documentation / 文档

- [文档总索引](docs/README.md)
- [用户帮助](docs/help.md)
- [版本状态与升级说明](docs/version.md)
- [完整更新历史](docs/history.md)
- [v1.12.0 功能完成度与发布检查](docs/RELEASE_READINESS.md)
- [开发者项目指南](docs/AI_PROJECT_GUIDE.md)
- [工程优化与重构 TODO](docs/ENGINEERING_TODO.md)
- [未来产品与架构规划](docs/FUTURE_ROADMAP.md)
- [测试指南](TESTING.md)
- [插件 SDK](src/plugin/README.md)

## Current focus / 当前重点

- v1.12.0 adds stereo spectrum analysis, transient note drafts, manual BPM measurement/interpolation, an independent BPM curve window, and chart/audio merging.
  v1.12.0 新增双声道频谱分析、瞬态音符草稿、手动 BPM 测量与插值、独立 BPM 曲线窗口，以及谱面与音频合并。
- Unified shortcuts, settings transfer, independent drag snapping and configurable Hyperfruit judging are implemented. Unfinished Timbre/Tracks tools are hidden.
  统一快捷键、设置迁移、独立拖动吸附和可选红果判定均已实现；尚未完成的音色／伪轨道工具不显示入口。
- Local multi-difficulty projects, layers, comments and `.ccepr` review are implemented as optional experimental workflows. Feature boundaries and remaining release checks are tracked in [RELEASE_READINESS.md](docs/RELEASE_READINESS.md).
  本地多难度工程、图层、评论和 `.ccepr` 审阅已有可选的实验性实现；功能边界与剩余发布检查见[发布检查表](docs/RELEASE_READINESS.md)。

完整变更见 [docs/history.md](docs/history.md) 顶部，近期维护见 [docs/ENGINEERING_TODO.md](docs/ENGINEERING_TODO.md)，长期方向见 [docs/FUTURE_ROADMAP.md](docs/FUTURE_ROADMAP.md)。

## Features / 功能

- Load, create, edit, save, and export Malody Catch `.mc` / `.mcz` charts.
- Place, move, select, copy, paste, mirror, and delete Normal / Sound / Rain notes.
- BPM table editing, automatic BPM measurement, timeline navigation, and playback-speed control.
- Analysis Editor with stereo spectrum, transient inspection, note drafts and reusable analysis profiles.
- Manual BPM measurement/interpolation and a modeless synchronized BPM curve editor.
- Chart/audio merging with per-segment speed, pitch, fades and layered previews.
- Optional experimental `.cceproj` projects, shared layers, local comments and `.ccepr` proposal review.
- Skin rendering, division colors, Hyperfruit hints, background images, note sounds, and realtime preview.
- Hyperfruit hints use Malody E judge by default; Settings → Hyperfruit Judge lets you select A–E and Pro for both the canvas and realtime preview.
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
5. Drag a panel header to dock or float it; the live outline shows the target region, while the chart workspace remains a protected central area. Reopen closed panels from `View -> Panels`. Classic and multi-window layouts are saved independently.
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

AutoTimingCore is pinned at `23d3164b8a3fa2d8e9039a4fa86c8c029fc8b8fd`.
Its Malody legacy source license is not confirmed; see
[docs/AUTOTIMING_VENDORING.md](docs/AUTOTIMING_VENDORING.md) and the submodule's
`ATTRIBUTION.md` before redistribution.

Special thanks to **myhome** for the included skin: [skin page](https://m.mugzone.net/store/skin/detail/5982).
