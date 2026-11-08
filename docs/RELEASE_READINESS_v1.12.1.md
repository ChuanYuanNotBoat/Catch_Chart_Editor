# Beta v1.12.1 发布准备记录

检查日期：2026-11-08

状态：候选版本准备中；本记录所列待完成事项完成前，不作为正式发布记录。

上一已发布版本：Beta v1.12.0（2026-10-25）。

## 发布内容与更新日志核对

本版更新日志覆盖本次合入的三项用户可见变更：

- 音频播放与频谱共用输出设备时钟，修复重复播放及音频设备下的显示偏移问题。
- Malody Catch 到 osu! Catch 导出新增字段设置、可手选的保存目录和打开实际输出目录的选项。CS 支持直接输入及基于 Catch 判定宽度换算的 A–E、Pro 快捷预设。转换器校验输入字段和数值范围，避免无效配置导出。
- 经典侧栏中的插件工具按可用内容高度布局，加载和展开后可完整显示。

CS 快捷预设值如下。

| Malody 判定 | A | B | C | D | E |
|---|---:|---:|---:|---:|---:|
| 普通 | 2.1061 | 3.3105 | 4.1134 | 4.7558 | 5.3981 |
| Pro | 2.6079 | 3.7521 | 4.5149 | 5.1251 | 5.7354 |

## 当前核对状态

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
| Malody 客户端实际导入检查 | 待人工验收 | 使用真实客户端确认生成的 .osz 内容与音频资源 |
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
