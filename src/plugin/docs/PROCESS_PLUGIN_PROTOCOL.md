# Process Plugin Protocol (Host API v2)

适用于 Python / Node.js / Rust / Go 等任意语言插件。

## 1. Discovery

宿主会在 `{appDir}/plugins` 扫描 `*.plugin.json` 文件作为插件清单。

## 2. Manifest Schema

示例：

```json
{
  "pluginId": "demo.py.echo",
  "displayName": "Python Echo Plugin",
  "version": "0.1.0",
  "description": "Process plugin example",
  "author": "Your Name",
  "pluginApiVersion": 2,
  "executable": "python",
  "args": ["./py_echo_plugin.py"],
  "capabilities": ["chart_observer"],
  "localizedDisplayName": {
    "zh_CN": "Python 回显插件",
    "en_US": "Python Echo Plugin"
  },
  "localizedDescription": {
    "zh_CN": "多语言进程插件示例",
    "en_US": "Example process plugin"
  }
}
```

必填字段：
- `pluginId`
- `displayName`
- `version`
- `description`
- `author`
- `pluginApiVersion`
- `executable`

选填字段：
- `args`（字符串数组）
- `capabilities`（字符串数组）
- `localizedDisplayName`（对象）
- `localizedDescription`（对象）

## 3. Transport

- 宿主启动外部进程
- 宿主向插件 `stdin` 写入一行 JSON（UTF-8）
- 插件从 `stdin` 逐行读取
- 插件向 `stdout` 逐行输出 JSON（仅用于 request-response）

每条消息必须单行，以 `\n` 结束。

## 4. Message Types

### 4.1 Notification (Host -> Plugin)

```json
{"type":"notify","event":"initialize","payload":{"plugin_id":"demo.py.echo","locale":"zh_CN","host_api_version":2}}
{"type":"notify","event":"onChartChanged"}
{"type":"notify","event":"onChartLoaded","payload":{"chart_path":"D:/beatmap/test.mc"}}
{"type":"notify","event":"onChartSaved","payload":{"chart_path":"D:/beatmap/test.mc"}}
{"type":"notify","event":"shutdown"}
```

### 4.2 Request (Host -> Plugin)

```json
{"type":"request","id":"1713512345678","method":"openAdvancedColorEditor","payload":{"feature":"note_color_editor"}}
```

```json
{"type":"request","id":"1713512345680","method":"listToolActions","payload":{}}
```

```json
{"type":"request","id":"1713512345681","method":"runToolAction","payload":{"action_id":"standardize_all_colors","context":{"chart_path":"D:/beatmap/test.mc","locale":"zh_CN","language":"zh"}}}
```

### 4.3 Response (Plugin -> Host)

```json
{"type":"response","id":"1713512345678","result":true}
```

`result` 默认按 `bool` 处理；对于 `listToolActions` 返回数组。

对于 `listToolActions`，`result` 应为数组，元素结构建议：

```json
{
  "action_id": "standardize_all_colors",
  "title": "标准化所有颜色",
  "description": "可选描述",
  "confirm_message": "执行前确认文案",
  "placement": "left_sidebar",
  "requires_undo_snapshot": true,
  "checkable": false,
  "checked": false,
  "sync_plugin_tool_mode_with_checked": false
}
```

当 `sync_plugin_tool_mode_with_checked=true` 且该 action 为 `checkable` 时，Host 会在动作执行后按最新 `checked` 状态自动切换插件增强工具模式：
- `checked=true`：启用插件工具模式
- `checked=false`：退出插件工具模式（回归原始编辑状态）

`placement` 可选值：
- `tools_menu`
- `top_toolbar`
- `left_sidebar`
- `right_note_panel`
- `plugin_context_menu`

### 4.4 Optional Request: Host Batch Edit

Host may ask plugin to return a batched edit payload:

```json
{"type":"request","id":"1713512345682","method":"buildBatchEdit","payload":{"action_id":"your_action","context":{"chart_path":"D:/beatmap/test.mc"}}}
```

Plugin response `result` object schema:

```json
{
  "add": [ { "beat":[0,1,4], "x":256, "type":0, "id":"optional-id" } ],
  "remove": [ { "beat":[1,0,1], "x":128, "type":0, "id":"existing-id" } ],
  "move": [
    {
      "from": { "beat":[2,0,1], "x":200, "type":0, "id":"n1" },
      "to":   { "beat":[2,1,1], "x":260, "type":0, "id":"n1" }
    }
  ]
}
```

When host applies this result, it is committed as ONE undo step.

### 4.5 Optional Request: Canvas Overlay

Host may request per-frame overlay draw items:

```json
{"type":"request","id":"1713512345683","method":"listCanvasOverlays","payload":{"canvas_width":1200,"canvas_height":800}}
```

Plugin response `result` is an array. Item schema:

```json
{
  "kind":"line|rect|text",
  "x1":10,"y1":20,"x2":300,"y2":400,
  "x":20,"y":30,"w":200,"h":80,
  "text":"Overlay",
  "color":"#FF0000",
  "fill_color":"#44FF0000",
  "width":2.0,
  "font_px":12
}
```

Optional chart-space coordinates (for note-like scrolling stability):

```json
{
  "kind":"line|rect|text",
  "coord_space":"chart",
  "lane_x1":256, "beat1":32.0,
  "lane_x2":300, "beat2":33.0,
  "lane_x":256, "beat":32.0,
  "rect_anchor":"center|top_left"
}
```

When `coord_space` is `chart`, host converts `(lane_x, beat)` to canvas each frame using current scroll/playback state.

### 4.6 Dockable Tool UI and Floating Panels

Process plugins cannot return an embedded QWidget directly. Every discovered
tool action is therefore also rendered in the host-owned, ADS-compatible
`Plugin Tools` panel. Use `scope_selector: "note_range"` to request the standard
selected-notes / beat-range / entire-chart GUI; the resulting context is passed
to `runToolAction` as `format_scope`, `range_start_beat`, and
`range_end_beat`. Other actions receive a normal host button.

Native plugins may still return QWidget-based floating panels. The host inserts
those panels into the same vertically split tool column by default, while
keeping every panel independently detachable, closable, and restorable. Docked
tool panels remain simultaneously visible rather than becoming switching tabs.

## 5. i18n Metadata Fallback

宿主读取本地化字段时按顺序匹配：
1. 完整 locale（如 `zh_CN`）
2. 语言码（如 `zh`）
3. `default`
4. 非本地化字段（`displayName` / `description`）

## 6. Error Guidance

- 插件应忽略未知 `event` / `method`
- 插件内部异常不应导致进程崩溃
- 对无法处理的 `request` 返回 `result=false`


## 7. Optional Locale Context

- Host may include locale (e.g. zh_CN) and language (e.g. zh) in request context.
- Plugins can ignore these fields safely if not needed.
- For one-shot mode (--run-tool-action), host also exports:
  - MALODY_LOCALE
  - MALODY_LANGUAGE

## 8. Host API v3 Extensions (Addendum)

When plugin declares `pluginApiVersion: 3`, host may call additional methods:

- `handleCanvasInput` (interactive canvas tool-mode input)
- `getPanelWorkspaceConfig` (dock/merge workspace hints)

### 8.1 handleCanvasInput

Request payload:

```json
{
  "context": {"canvas_width":1200,"canvas_height":800,"tool_mode_active":true},
  "event": {
    "type":"mouse_move",
    "x":420.0,
    "y":260.0,
    "button":0,
    "buttons":1,
    "modifiers":0,
    "wheel_delta":0.0,
    "key":0,
    "timestamp_ms":1713512345684
  }
}
```

Response result object:

```json
{
  "consumed": true,
  "overlay": [],
  "preview_batch_edit": {"add":[],"remove":[],"move":[]},
  "cursor": "crosshair",
  "status_text": "Dragging control point"
}
```

### 8.2 getPanelWorkspaceConfig

Request payload: normal context object.

Response result example:

```json
{
  "workspace_id": "note_chain_workspace",
  "docking_supported": true,
  "tab_merge_supported": true,
  "default_layout": "advanced"
}
```
