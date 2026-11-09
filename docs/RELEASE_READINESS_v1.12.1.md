# Beta v1.12.1 发布准备记录

初次检查日期为 2026-11-08，当前候选复验日期为 2026-11-09。

Windows x64 Beta v1.12.1 已进入发布候选验收。全新并行构建和自动回归通过，实际安装与客户端兼容性仍待验收。

上一已发布版本：Beta v1.12.0（2026-10-25）。

## 2026-11-09 子模块迁移后的候选验收

本轮 Windows Release 从 CCE 候选源码提交 `e8c0436` 全新递归克隆构建。以下为该候选版本的验收状态。

| 项目 | 结果 | 验收依据 |
|---|---|---|
| D 转谱依赖 | 已固定 | `third_party/MalodyCatch2CTB` 固定至公开提交 `c40c8a6d1385b6e024d35c8df27b00bb904d1c07`，插件版本为 `0.2.0`。全新递归克隆从远端取回两个子模块。关闭插件的配置也通过。 |
| Windows Release 构建 | 通过 | 使用 MSVC v143 14.44、Qt 6.10.2 x64 与 LDC 1.43.0，从空构建目录并行构建。主程序先于分析测试部署 Qt，先前的 DLL 复制冲突未再出现。 |
| 自动回归 | 41 个目标分段通过 | 39 个常规目标及播放时钟、分析编辑器目标在无界面条件下执行。分析用例使用静音播放与本地 OGG。测试前后本机配置及恢复文件的校验值一致。 |
| Catch 导出 | 通过 | 上游 12 项 D 转换测试和 5 项 Python 回归通过。CCE 界面复验当前难度、手选子集、目录全部及工程全部四条路径，包内每个难度的参数和音频均符合设置。两份真实 `.mcz` 样本另验证了六个难度的重复转换。 |
| Windows 运行库部署 | 通过 | 从 MSVC 可再分发目录复制所需 DLL 至编辑器同目录。打包脚本检查 `msvcp140.dll`、`msvcp140_1.dll`、`vcruntime140.dll` 和 `vcruntime140_1.dll`，同时核对插件二进制、清单、说明及 MIT 许可证。 |
| Inno Setup 候选安装包 | 编译和清单核对通过 | 程序报告 `Beta v1.12.1`。对照安装器收录文件与二进制导入表，已确认所需 Visual C++ DLL 随包提供，插件二进制与子模块构建产物一致。测试程序和调试符号已排除。 |
| Windows 安装体验 | 待验收 | 在干净 Windows 上验证安装、启动、从 v1.12.0 升级与卸载。 |
| 客户端与系统交互 | 待验收 | 在 osu! 和 Malody 分别导入 `.osz` 与 `.mcz`。实际打开所选导出目录、听音和切换设备仍待实测。 |
| AutoTiming legacy 分发 | 待维护者确认 | 当前固定提交的来源声明仍记载 legacy 源码授权未确认。见 [依赖说明](AUTOTIMING_VENDORING.md)。 |

候选安装包路径为 `dist/v1.12.1-rc-20261109/CatchChartEditor_Beta_v1.12.1_Setup.exe`，同目录提供 SHA-256 校验文件。构建、测试和安装包清单日志保存在 `build/v1.12.1-release-prep-20261109/`。

## 2026-11-09 初版复验

本轮验收 Windows x64 Beta v1.12.1，源码为 `0748c01`。使用空目录重新配置并构建 Release，验收在无界面、静音条件下执行。下方 11 月 8 日的构建和打包结果保留为历史记录。

| 项目 | 结果 | 验收依据 |
|---|---|---|
| 全新 Release 构建 | 并行失败，串行通过 | 主程序与 `AnalysisEditorTests` 同时向 `Release` 部署 Qt。首次构建复制 `Qt6Network.dll` 时因目标文件已存在而失败。用 `--parallel 1` 重试完成，默认并行发布脚本仍受影响。 |
| Release 自动回归 | 41 个目标分段通过 | 39 个常规目标和 `audio_playback_timing_tests` 使用原测试程序。`analysis_editor_tests` 使用仅增加静音设置的临时测试副本，46 项通过。首次跳过的外部音频用例随后以本地 OGG 补测通过。 |
| Windows 音频时钟 | 通过 | `outputClockReanchorsAfterPauseAndSeek` 确认实际进入 WASAPI 时钟路径，覆盖暂停、连续定位、变速和末尾重播。分析编辑器的合成音频与真实 OGG 循环播放均通过。 |
| Catch 导出 | 通过 | 两份本地 `.mcz` 样本共六个 Catch 难度，均在含中文与空格的目录重复导出。验证了 `.osz` 内难度数、Catch 模式、音频内容和源文件哈希。 |
| Inno Setup 安装包 | 编译通过 | 运行版本为 Beta v1.12.1。打包清单包含默认皮肤、Qt、D 转换器及许可证和声明，排除了测试二进制和调试符号。 |
| Windows 干净安装 | 有发布阻塞 | 主程序及 Qt 依赖 `msvcp140.dll`、`msvcp140_1.dll`、`vcruntime140.dll` 和 `vcruntime140_1.dll`，本次打包清单缺少这些文件，安装脚本也未安装或检查所需运行库。本机已有该运行库。缺少运行库的机器无法直接启动，实际安装、升级与卸载仍待验证。 |
| 客户端与系统交互 | 待验收 | 需分别在 osu! 中导入 `.osz`、在 Malody 中导入 `.mcz`，并确认资源和播放。在 Windows 实际打开用户所选导出目录，以及听音和切换设备仍待实测。 |
| AutoTiming legacy 分发 | 待维护者确认 | 当前固定提交的来源声明仍记载 legacy 源码授权未确认。见 [依赖说明](AUTOTIMING_VENDORING.md)。 |

更新日志尚未提及切换音符工具时保留曲线编辑状态的修复（`068136f`）。导出设置的新标签已有简体中文翻译，日文资源仍缺部分标签。

本轮安装包位于 `dist/v1.12.1-acceptance-20261109/CatchChartEditor_Beta_v1.12.1_Setup.exe`，共 32,206,692 字节。SHA-256 为 `8925A2B57BAB5049C1E6693959991E4064308B559AADA544554189F44AF61673`。

构建、测试、转换与打包原始日志保存在 `build/v1.12.1-acceptance-20261109/`。

## 2026-11-08 初版更新日志核对

本版更新日志覆盖本次合入的三项用户可见变更：

- 音频播放与频谱共用输出设备时钟，修复重复播放及音频设备下的显示偏移问题。
- Malody Catch 到 osu! Catch 导出新增字段设置、可手选的保存目录和打开实际输出目录的选项。CS 支持直接输入及基于 Catch 判定宽度换算的 A–E、Pro 快捷预设。转换器校验输入字段和数值范围，避免无效配置导出。
- 经典侧栏中的插件工具按可用内容高度布局，加载和展开后可完整显示。

CS 快捷预设值如下。

| Malody 判定 | A | B | C | D | E |
|---|---:|---:|---:|---:|---:|
| 普通 | 2.1061 | 3.3105 | 4.1134 | 4.7558 | 5.3981 |
| Pro | 2.6079 | 3.7521 | 4.5149 | 5.1251 | 5.7354 |

## 2026-11-08 核对状态

| 项目 | 状态 | 记录 |
|---|---|---|
| CMake、运行时与安装器版本 | 已核对 | 统一为 1.12.1，安装文件名使用 Beta v1.12.1 |
| 用户更新日志与文档索引 | 已整理 | history.md 收录本次功能、修复与 CS 对照表；旧版本记录保持独立 |
| Windows Debug 构建 | 已通过 | 编辑器与 Catch 导出插件均完成构建 |
| Catch 导出回归 | 已通过 | CtbProcessPluginTests 和 ctb_converter_smoke 覆盖自选目录、字段覆盖、非法输入与重复导出 |
| Hyperfruit 回归 | 已通过 | HyperfruitTests 通过 |
| Windows 实际打开导出目录 | 待人工检查 | 代码使用对话框实际选择的输出目录打开文件管理器，仍需确认窗口跳转位置 |
| Windows Release 构建 | 已通过 | 清洁提交 d9dfdc8 的 Release 编译成功；运行时报告 Beta v1.12.1 |
| Windows 全量测试 | 已通过 | 此候选提交的 41 项测试全部通过；全量构建阶段共编译 41 项测试目标 |
| Inno Setup 发行安装包 | 已生成 | 30.7 MB，发行清单检查和安装包编译通过，路径为 dist/v1.12.1 |
| Git 功能与版本提交 | 已提交 | d9dfdc8，feat(release): prepare beta v1.12.1 candidate |
| 客户端实际导入检查 | 待人工验收 | 在 osu! 中检查 `.osz`，在 Malody 中检查 `.mcz`，分别确认谱面与音频资源 |
| 正式版本标签、发布页和上传 | 未执行 | 候选版本准备完成后再单独执行 |

## 发行包清单

发行脚本应自动检查编辑器、Qt 运行库、默认皮肤、插件运行文件、许可证及随程序分发的文档。预期安装包为：

dist/v1.12.1/CatchChartEditor_Beta_v1.12.1_Setup.exe

安装包、构建文件和正式发布标签均不等同于正式公开发布。

## 构建与打包命令

从仓库根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File tools/packaging/build_and_package.ps1 -Configuration Release -BuildDir build_clion_msvc -OutputDir dist/v1.12.1
```

发行脚本、测试套件和安装包编译均已执行。清洁源码提交和发行包版本均为 d9dfdc8，Debug／Release 构建的运行时版本均为 Beta v1.12.1。

Release 全量构建、41 项测试及 Inno Setup 编译均已通过。MSVC 输出包含 C4819 和 C4244 警告；windeployqt 另提示 VCINSTALLDIR 不可用，这些提示没有导致构建或打包失败。
