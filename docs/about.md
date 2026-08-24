# 关于 Malody Catch Editor

Malody Catch Editor 是面向 Malody Catch 模式的开源桌面谱面编辑器，重点补充官方工具在批量编辑、曲线制谱、谱面检查、资源管理和可组合工作区方面的能力。

## 项目状态

- 当前开发周期：**Beta v1.11.0（开发中）**
- 最新发布：**Beta v1.10.5**
- 主要桌面验证平台：Windows
- 技术栈：C++17、Qt 6 Widgets、Qt Multimedia、Qt Advanced Docking System

Beta 表示项目已经可用于实际制谱，但仍可能调整交互、sidecar 细节和插件 API。编辑重要谱面时请保留源文件与 sidecar 备份。

## 项目原则

- 保持 Malody `.mc` / `.mcz` 兼容。
- 编辑器扩展数据与官方谱面数据分离。
- 高频画布交互优先使用内部 C++ 实现。
- 面板应可停靠、拆分、组合和浮动，不强制占用固定侧栏高度。
- 重要修改应有可重复的 Debug/Release 测试或明确手工回归记录。

## 链接

- 仓库与反馈：https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor
- 最新发布：https://github.com/ChuanYuanNotBoat/Malody_Catch_Editor/releases/latest
- [文档索引](README.md)
- [用户帮助](help.md)
- [更新历史](history.md)

## 许可与致谢

项目主体采用 GPL-3.0。vendored Qt Advanced Docking System 5.1.1 采用 LGPL-2.1，源码与许可证位于 `third_party/QtAdvancedDockingSystem/`。

感谢 Qt、Qt Advanced Docking System、Malody 社区、测试者和贡献者。默认皮肤素材经 **myhome** 授权用于本项目，来源见根目录 README。
