# Malody Catch Editor 文档索引

> 当前版本：**Beta v1.11.1（2026-09-07）**
> 仓库状态：**Unreleased maintenance（2026-09-11）**
> Git 标签：待发布
> 索引最后核对：2026-09-11

## 用户文档

| 文档 | 用途 |
|------|------|
| [help.md](help.md) | 菜单、可组合面板、画布操作、曲线工具、BPM、Meta、插件与快捷键 |
| [version.md](version.md) | 版本、兼容性和升级注意事项 |
| [history.md](history.md) | 按版本维护的完整更新记录；当前维护内容位于顶部 `Unreleased` 段 |
| [about.md](about.md) | 项目定位、支持范围、许可证与反馈入口 |

## 开发与维护

| 文档 | 用途 |
|------|------|
| [AI_PROJECT_GUIDE.md](AI_PROJECT_GUIDE.md) | 当前源码结构、关键数据流、构建测试和维护约束 |
| [ARCHITECTURE_FORMAT_REFERENCE.md](ARCHITECTURE_FORMAT_REFERENCE.md) | `.mc` / `.mcz`、模型、坐标、插件、sidecar 和 Malody 引擎兼容参考 |
| [NOTE_CHAIN_EDITOR.md](NOTE_CHAIN_EDITOR.md) | 原生 C++ 曲线编辑器的状态、交互、持久化、撤销和性能约束 |
| [Autotiming.md](Autotiming.md) | AutoTiming 自动节拍检测流程与公共接口 |
| [ENGINEERING_TODO.md](ENGINEERING_TODO.md) | 已审计的性能/可靠性事项、验收门槛与未来重构决策 |
| [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md) | 多难度、图层剪贴板/跨 MC 转移、Meta 同步、新建 MC、审图、版本、osu!ctb、移动端与通用核心的长期规划 |
| [../TESTING.md](../TESTING.md) | 自动化测试目标、命令、覆盖范围和手工回归清单 |

## 插件开发

插件 SDK 总入口：[../src/plugin/README.md](../src/plugin/README.md)

| 文档 | 用途 |
|------|------|
| [../src/plugin/docs/PROCESS_PLUGIN_PROTOCOL.md](../src/plugin/docs/PROCESS_PLUGIN_PROTOCOL.md) | JSON-lines 外部进程插件协议 |
| [../src/plugin/docs/CANVAS_INTERACTION_PROTOCOL.md](../src/plugin/docs/CANVAS_INTERACTION_PROTOCOL.md) | Host API v3 画布输入与 overlay 协议 |
| [../src/plugin/docs/ADVANCED_COLOR_EDITOR_PLUGIN.md](../src/plugin/docs/ADVANCED_COLOR_EDITOR_PLUGIN.md) | 高级颜色编辑能力约定 |
| [../src/plugin/docs/PLUGIN_TEMPLATE.md](../src/plugin/docs/PLUGIN_TEMPLATE.md) | 原生插件最小模板 |
| [../plugins/samples/README.md](../plugins/samples/README.md) | 可复制的插件样例目录 |

## 文档维护规则

1. **未发布内容单独记录。** 发布后的新增、修复和重构写入 `history.md` 顶部的 `Unreleased` 段，发版时再归入新版本。
2. **发布记录冻结。** 已发布版本的历史条目只允许修正事实或错字，不追加后续开发内容。
3. **区分版本状态。** `readme.md` 和 `version.md` 标出当前版本与历史版本；下载链接指向最新发布版本。
4. **以当前 C++ 实现为准。** Note Chain 的权威实现是 `src/editor/NoteChain/`，旧 Python 插件只能作为兼容输入来源，不能作为现行行为规范。
5. **区分路线图、执行清单与临时报告。** 长期产品/架构方向维护在 `FUTURE_ROADMAP.md`，近期可执行事项维护在 `ENGINEERING_TODO.md`；一次性分支快照、原始审计草稿和重复迁移笔记不提交。
6. **同步入口。** 新增、重命名或删除长期文档时，必须同步本索引及相关 README 链接。
