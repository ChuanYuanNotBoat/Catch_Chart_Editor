# Advanced Color Editor Capability

本文档定义 `advanced_color_editor` 能力的宿主约定。

> 兼容 Host API v2-v3；最后核对：2026-09-10。

## 1. Capability Key

插件需要在 `capabilities` 中声明：

- `advanced_color_editor`

## 2. Entry Point

宿主调用：
- Native 插件：`openAdvancedColorEditor(const QVariantMap& context)`
- Process 插件：`request(method="openAdvancedColorEditor", payload=context)`

返回：
- `true`：插件已处理
- `false`：宿主回退到内置行为

## 3. Context Payload

当前约定字段：
- `feature`: 固定值 `note_color_editor`
- `chart_path`: 当前谱面绝对路径
- `target_note_ids`: 目标 note id 列表
- `safe_denominators`: 安全分母列表（用于保持时间精度）
- `time_division`: 当前编辑器时间分割

后续字段只做追加，不删除已有字段。

## 4. Safety Rule

- 默认应保证时值不漂移
- 破坏性修改应要求明确确认
- 上下文不足时应安全失败并返回 `false`

## 5. Error Handling

插件抛出的异常由宿主捕获并记录日志，不应导致主程序崩溃。


## 6. Optional i18n Context

Host may include these optional fields in context:
- locale (e.g. zh_CN)
- language (e.g. zh)

Plugins can ignore them safely, or use them to load plugin-localized UI/resources.
