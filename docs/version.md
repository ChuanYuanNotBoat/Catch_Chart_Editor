# 版本说明

## 当前状态

- 当前版本：**Beta v1.11.0-patch.1（2026-08-31）**
- 上一版本：**Beta v1.11.0（2026-08-27）**
- Git 标签：`v1.11.0-patch.1`

本版本为 v1.11.0 的补丁发布，变更见 [history.md](history.md) 顶部的 `Beta v1.11.0-patch.1` 记录。已冻结的 v1.11.0 历史不回填该补丁内容。

## 兼容性

- 谱面：保持 Malody Catch `.mc` JSON 兼容，不写入编辑器私有字段。
- 打包：支持 `.mcz` 导入/导出；发布前仍需使用真实 Malody 客户端验证包结构。
- 曲线项目：使用 V3 `.mcce-plugin/*.curve_tbd.json`，兼容读取旧 Python anchors/handles/links。
- 布局：Beta v1.11.0 使用 ADS 布局状态；损坏或不兼容状态会回退默认布局，可从 `View -> Panels -> Reset Panel Layout` 手动重置。
- 插件：Host API 当前为 v3，支持 API v2-v3；旧 `builtin.note_chain_assist` 被原生 C++ 模块替代并由宿主跳过。

## 升级注意

- 首次运行 v1.11.0 后，建议检查所有浮动面板所在显示器和布局恢复结果。
- 从旧版本打开曲线 sidecar 后，保存会规范化为当前 V3 表达；重要工程建议先保留备份。
- 外部进程插件仍依赖其运行环境，例如 Python 插件需要可用的 `python` 命令。
- 如面板布局异常，先执行 `View -> Panels -> Reset Panel Layout`，再重新排列并正常退出以保存。

本页会在程序的版本信息窗口中与运行时版本、Qt、ABI、系统信息和 [history.md](history.md) 一起显示。
