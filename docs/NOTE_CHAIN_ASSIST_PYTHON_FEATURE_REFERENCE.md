# Note Chain Assist（曲线插件）Python 实现功能与 GUI 布局参考

> 本文档是内置"曲线 / Note 串"插件 `builtin.note_chain_assist`（**Python 外部进程实现**）的**完整功能规格与 GUI 布局参考**，用作 C++ 内部重写（`src/editor/NoteChain/`）**功能对等审计**的基准。
>
> 范围：只覆盖 Python 实现（`plugins/builtin/note_chain_assist/`）。C++ 内部实现的问题不在本文讨论，见 `docs/NOTE_CHAIN_ASSIST_CURVE_PLUGIN_AUDIT.md`。

---

## 目录

1. 文档目的与对照方法
2. 插件元信息（Manifest）
3. GUI 布局总览
4. 完整 Tool Action 清单（对照表）
5. 画布交互完整清单
6. 运行时状态树（STATE）
7. 坐标与时间数学
8. 曲线模型与采样
9. 批处理提交（buildBatchEdit）
10. sidecar V3 持久化格式
11. 撤销 / 重做模型
12. i18n 本地化
13. 协议方法清单
14. C++ 内部实现对等检查清单
15. 附录：文件对应与已知差异

---

## 1. 文档目的与对照方法

**目的**：Python 插件是当前"曲线编辑"的**权威行为来源**（`docs/todo_native_notechain_migration.md` 记录其向 C++ 原生迁移尚未完成，默认仍走 Python）。当 C++ 内部实现（`src/editor/NoteChain/`）逐步接管功能时，必须以本文档记录的行为为"黄金标准"逐项比对。

**对照检查方法**：

1. 从第 4 章「Tool Action 清单」核对：C++ 侧暴露的 action 数量、`placement`、`checkable`、`requires_undo_snapshot`、默认 `checked` 是否一致。
2. 从第 5 章「画布交互清单」核对：每个鼠标/键盘/手势事件的处理路径与消费（consumed）语义是否一致。
3. 从第 6 章「状态树」核对：运行时状态字段（尤其 anchors/links/segment_* / groups）是否字段级对齐。
4. 从第 7~9 章核对：坐标转换、曲线采样、批处理提交的**数值语义**是否一致（这是最容易产生偏差的地方）。
5. 从第 10 章核对：sidecar V3 序列化/反序列化的**字段名与 triplet 语义**是否一致。
6. 最后用第 14 章「对等检查清单」逐项勾选。

> 文中所有行为均可回溯到 Python 源码出处（`note_chain_assist.py` 及其 `modular/` 子模块）。

---

## 2. 插件元信息（Manifest）

清单文件：`plugins/builtin/note_chain_assist/note_chain_assist.plugin.json`

```json
{
  "pluginId": "builtin.note_chain_assist",
  "displayName": "Note Chain Assist",
  "version": "1.0.0",
  "description": "Interactive pen-curve tool for direct note-chain editing on canvas",
  "author": "Malody Catch Editor",
  "pluginApiVersion": 3,
  "executable": "python",
  "args": ["./note_chain_assist.py", "--plugin"],
  "capabilities": [
    "tool_actions",
    "canvas_overlay",
    "canvas_interaction",
    "panel_workspace",
    "host_batch_edit"
  ],
  "localizedDisplayName": {
    "zh_CN": "Note 串编辑增强",
    "ja_JP": "ノートチェーン補助"
  },
  "localizedDescription": {
    "zh_CN": "在主编辑区直接进行钢笔曲线编辑并批量生成常规 note",
    "ja_JP": "メイン編集領域でペン曲線を直接編集し、通常ノートを一括生成します"
  }
}
```

### 能力（capabilities）语义

| 能力                   | 含义                                 | 对应宿主请求                            |
| ---------------------- | ------------------------------------ | --------------------------------------- |
| `tool_actions`       | 提供可点击的动作（工具栏/面板/菜单） | `listToolActions` / `runToolAction` |
| `canvas_overlay`     | 每帧在画布上绘制叠加元素             | `listCanvasOverlays`                  |
| `canvas_interaction` | 接收主画布交互事件并可消费           | `handleCanvasInput`                   |
| `panel_workspace`    | 声明停靠/合并/多窗口工作区           | `getPanelWorkspaceConfig`             |
| `host_batch_edit`    | 返回一次性批量谱面编辑（单 undo 步） | `buildBatchEdit`                      |

> 插件声明 `pluginApiVersion: 3`。宿主 `PluginInterface::kHostApiVersion == 3`，`kMinSupportedPluginApiVersion == 2`。

## 3. GUI 布局总览

曲线插件以「**可切换工具（tool mode）**」方式集成到主编辑器，而非独立编辑器窗口。它不修改宿主默认编辑手势，只在进入插件工具态时接管画布交互。

### 3.1 入口

- **主要入口**：右侧 Note 面板的「放置锚点（Anchor Place）」可勾选动作。该动作声明 `sync_plugin_tool_mode_with_checked: true`——勾选即进入插件工具态，取消勾选即退出并回归宿主默认编辑。
- **兜底入口**：宿主「工具（Tools）」菜单中的 `export_style_preset` / `import_style_preset`（样式导入导出）。
- 插件**不提供**嵌入的 QWidget 浮动面板（进程插件无法直接返回 QWidget），高级参数通过宿主各 placement 的动作承载。

### 3.2 各 UI 区域（placement 分布）

| 区域           | placement               | 承载动作                                                                                         |
| -------------- | ----------------------- | ------------------------------------------------------------------------------------------------ |
| 顶部工具栏     | `top_toolbar`         | 提交曲线`commit_curve_to_notes`                                                                |
| 右侧 Note 面板 | `right_note_panel`    | 放置锚点 / 显示曲线 / 折线连接 / Note 吸附曲线 / 选择锚点 / 选择段 / 选择 note（全部 checkable） |
| 左侧边栏       | `left_sidebar`        | 提交曲线 / 重置曲线 / 连接选中 / 断开选中段                                                      |
| 右键上下文菜单 | `plugin_context_menu` | 提交右键段 / 切换右键段曲线-折线 / 连接选中 / 断开选中（含删锚点） / 密度固定·跟随·混合        |
| 工具菜单       | `tools_menu`          | 导出样式 / 导入样式                                                                              |

### 3.3 布局示意（ASCII）

```text
┌──────────────────────────── 主编辑区（画布）────────────────────────────┐
│  [画布 overlay：曲线预览线 / 控制点 / 手柄 / 采样点 / 标签文本 / 框选矩形] │
│                                                                       │
│    (左侧边栏)        (顶部工具栏)          (右侧 Note 面板)             │
│  ┌────────────┐   ┌──────────────┐   ┌──────────────────────┐        │
│  │ 提交曲线    │   │ [提交曲线]    │   │ ☑ 放置锚点(工具态)    │        │
│  │ 重置曲线    │   └──────────────┘   │ ☑ 显示曲线           │        │
│  │ 连接选中    │                       │ ☐ 折线连接           │        │
│  │ 断开选中段  │                       │ ☐ Note 吸附曲线      │        │
│  └────────────┘                       │ ☑ 选择锚点           │        │
│                                       │ ☑ 选择段             │        │
│   (右键菜单：提交段/形状/密度/连接)     │ ☐ 选择 note          │        │
│                                       └──────────────────────┘        │
│   (状态栏提示：status_text)                                            │
└───────────────────────────────────────────────────────────────────────┘
```

### 3.4 画布 overlay 元素（可独立开关）

overlay 由 `listCanvasOverlays` 返回，元素类型与开关：

| 元素                            | 开关（`overlay_toggles`）  | 说明                                                                     |
| ------------------------------- | ---------------------------- | ------------------------------------------------------------------------ |
| 预览曲线线（preview）           | `preview`（默认开）        | 逐段采样的折线段，选中段用金色`#FFD66B` 加粗，未选中用青色 `#33CCFF` |
| 采样点（sample_points）         | `sample_points`（默认开）  | 每段采样点每隔 4 个画一个 4×4 小方块                                    |
| 控制点 / 锚点（control_points） | `control_points`（默认开） | 锚点方块，选中/拖拽态变色                                                |
| 手柄（handles）                 | `handles`（默认开）        | in/out 手柄连线与手柄点                                                  |
| 标签（labels）                  | `labels`（默认开）         | 锚点标签`A{i}(平/角)` + 左上角密度摘要文本                             |
| 框选矩形                        | 无开关                       | 框选进行中显示                                                           |
| link 拖拽线                     | 无开关                       | Shift 拖拽连接时显示                                                     |

### 3.5 工作区配置（`getPanelWorkspaceConfig`）

```json
{
  "workspace_id": "note_chain_workspace",
  "docking_supported": true,
  "tab_merge_supported": true,
  "default_layout": "advanced",
  "window_group": "note_chain"
}
```

---

## 4. 完整 Tool Action 清单（对照表）

> 这是 C++ 对等审计的**核心表**。字段与 `PluginInterface::ToolAction` 对齐：`actionId` / `title` / `description` / `placement` / `requiresUndoSnapshot` / `checkable` / `checked` / `syncPluginToolModeWithChecked`。

### 4.1 固定动作（静态声明）

| action_id                            | placement           | requires_undo_snapshot | checkable | 默认 checked                                    | 功能                                                   |
| ------------------------------------ | ------------------- | ---------------------- | --------- | ----------------------------------------------- | ------------------------------------------------------ |
| `commit_curve_to_notes`            | top_toolbar         | true                   | false     | —                                              | 将整条曲线按段密度采样生成 NORMAL note，单 undo 步提交 |
| `commit_curve_to_notes_sidebar`    | left_sidebar        | true                   | false     | —                                              | 同上（侧边栏入口）                                     |
| `commit_context_segments_to_notes` | plugin_context_menu | true                   | false     | —                                              | 仅对右键命中的曲线段生成 note                          |
| `toggle_anchor_placement`          | right_note_panel    | false                  | true      | `anchor_placement_enabled`（默认 false）      | 进入/退出锚点放置工具态；**同步宿主工具模式**    |
| `toggle_curve_visible`             | right_note_panel    | false                  | true      | `curve_visible`（默认 true）                  | 显示/隐藏曲线 overlay                                  |
| `toggle_polyline_mode`             | right_note_panel    | false                  | true      | `active_link_shape=="polyline"`（默认 false） | 切换当前/后续链接的曲线↔折线                          |
| `toggle_note_curve_snap`           | right_note_panel    | false                  | true      | `note_curve_snap_enabled`（默认 false）       | 开启后宿主拖 note 可吸附到曲线                         |
| `toggle_select_anchors`            | right_note_panel    | false                  | true      | `selection_targets.anchors`（默认 true）      | 允许选择锚点                                           |
| `toggle_select_segments`           | right_note_panel    | false                  | true      | `selection_targets.segments`（默认 true）     | 允许选择曲线段                                         |
| `toggle_select_notes`              | right_note_panel    | false                  | true      | `selection_targets.notes`（默认 false）       | 允许选择（并透传）note                                 |
| `reset_curve`                      | left_sidebar        | true                   | false     | —                                              | 清空全部锚点/链接/段元数据，重置为默认                 |
| `connect_selected_nodes`           | left_sidebar        | false                  | false     | —                                              | 按选中锚点顺序两两连接                                 |
| `disconnect_selected_segments`     | left_sidebar        | true                   | false     | —                                              | 断开选中的曲线段                                       |
| `connect_selected_nodes_ctx`       | plugin_context_menu | false                  | false     | —                                              | 右键：连接选中锚点                                     |
| `disconnect_selected_segments_ctx` | plugin_context_menu | true                   | false     | —                                              | 右键：断开选中段 + 删除选中锚点                        |
| `toggle_context_polyline_mode`     | plugin_context_menu | false                  | false     | —                                              | 切换右键命中段的曲线↔折线                             |
| `export_style_preset`              | tools_menu          | false                  | false     | —                                              | 导出当前分母序列样式到`.nca_style.json`              |
| `import_style_preset`              | tools_menu          | false                  | false     | —                                              | 从`.nca_style.json` 导入分母序列                     |

### 4.2 动态动作（右键菜单，取决于命中/选中段密度）

仅当右键存在目标段（`density_has_target`）时追加：

| action_id                      | placement           | checkable | 功能                                             |
| ------------------------------ | ------------------- | --------- | ------------------------------------------------ |
| `set_segment_density_follow` | plugin_context_menu | true      | 段密度设为「跟随」默认分母                       |
| `set_segment_density_<N>`    | plugin_context_menu | true      | 段密度固定为`1/N`（N 来自 style.denominators） |
| `segment_density_mixed_info` | plugin_context_menu | true      | 仅当目标段密度混合时显示的信息项                 |

### 4.3 action_id 全集（`run_one_shot` 的 known 集合）

`commit_curve_to_notes` / `commit_curve_to_notes_sidebar` / `commit_context_segments_to_notes` / `toggle_anchor_placement` / `toggle_curve_visible` / `toggle_polyline_mode` / `toggle_context_polyline_mode` / `toggle_note_curve_snap` / `toggle_select_anchors` / `toggle_select_segments` / `toggle_select_notes` / `connect_selected_nodes` / `disconnect_selected_segments` / `connect_selected_nodes_ctx` / `disconnect_selected_segments_ctx` / `segment_density_mixed_info` / `export_style_preset` / `import_style_preset` / `reset_curve`，外加前缀 `set_segment_density_` 系列。

## 5. 画布交互完整清单

交互通过 `handleCanvasInput` 接收，事件类型 `mouse_down` / `mouse_move` / `mouse_up` / `wheel` / `key_down` / `key_up` / `focus_in` / `focus_out` / `cancel`。以下按类型逐一说明（出处：`modular/ui/input_handler.py`）。

### 5.0 通用前置

- 每次 `handleCanvasInput` 先执行：保存 `last_context`、`ensure_project_context`、`seed_missing_segment_denominators`、`sync_anchor_placement_with_host_mode`、`sync_anchor_selection_from_host_notes`。
- 若 `note_curve_snap_enabled` 且事件为鼠标左键系列，则**直接透传**（`consumed=false`），让宿主处理 note 吸附拖拽。
- 响应中 `overlay` 恒为 `[]`（性能考虑：宿主在 `handleCanvasInput` 后使 overlay 缓存失效，下一 tick 通过 `listCanvasOverlays` 重新拉取）；`preview_batch_edit` 恒为空。

### 5.1 鼠标按下（mouse_down）

**右键（button == RIGHT_BUTTON）**：调用 `set_context_menu_links_for_hit` 记录命中的段/锚点为 `context_menu_links`，返回 `consumed=false`（不消费，宿主仍弹右键菜单）。

**左键**，按命中优先级依次：

1. **手柄命中（hidx>=0）**：进入手柄拖拽态 `drag={mode: in|out, index}`，`cursor=crosshair`。
2. **锚点命中（aidx>=0）**：
   - `Shift` 按下 → 开始 **link 拖拽**（`link_drag.active=true`，source=该锚点），`cursor=pointing_hand`，不进入普通拖拽。
   - `Ctrl` 按下 → `toggle_selected_anchor`（切换该锚点选中）。
   - 无修饰 → `add_selected_anchor`（选中该锚点并清空段选择）。
   - **双击检测**（同锚点，间隔 ≤ 280ms）→ 切换 `smooth`（平滑↔折角），记历史。
   - 否则进入锚点拖拽态 `drag={mode: anchor, index}`，`cursor=size_all`。
3. **曲线段命中（seg_hit）**：`Ctrl` → `toggle_selected_link`；否则 `set_single_selected_link`；并同步锚点选中（段两端）。
4. **空白 + (Ctrl 或宿主 select 模式) 且非 note 选择**：开始框选 `box_select.active=true`（`append=ctrl`）。
5. **空白 + 无修饰**（`handle_mouse_down_empty_area`）：
   - note 可选中且 Anchor Place 关闭 → 透传（`consumed=false`）。
   - Anchor Place 关闭 → 提示「锚点放置已关闭」（`consumed=true`）。
   - 点击在可编辑 lane 之外 → 清空选择（不创建边界吸附锚点）。
   - 否则 `append_anchor` 添加锚点；若之前恰好选中 1 个锚点则自动连接并选中新锚点（快速连点链式流程）；随后进入新锚点拖拽态。

### 5.2 鼠标移动（mouse_move）

优先级：

1. **link 拖拽更新**（`link_drag.active`）→ 更新 hover 锚点。
2. **锚点拖拽 → link 拖拽切换**（`maybe_switch_anchor_drag_to_link_drag`，Shift 按下时）。
3. **框选更新**（`box_select.active`）→ 更新 end 点。
4. **拖拽编辑**（`drag.mode` 非空）：
   - `anchor`：canvas→chart 转换 + `snap_beat=true, snap_lane=false`，写 `lane_x/beat`，执行锚点/连接手柄时间约束，`cursor=size_all`。
   - `in`/`out`：写手柄绝对 chart 位置，`mirror=true`（smooth 时对称），执行手柄时间约束，`cursor=crosshair`。
5. 否则 hover 光标：命中手柄/锚点 → `pointing_hand`。

### 5.3 鼠标释放（mouse_up）

1. **link 拖拽结束**：`source_id` 与 `hover_anchor_id` 都 >0 且不同 → `add_link` 连接并记历史；否则取消。`consumed=true`。
2. **框选结束**：`apply_box_selection` 应用（选中框内锚点/段）。
3. **拖拽结束**：清空 `drag`，`record_history_state`（若拖拽过），`request_checkpoint=true`。
4. 无活动拖拽 → `consumed=false`。

### 5.4 滚轮（wheel）

Python 插件**不处理** wheel 事件：`handle_canvas_input` 中无 `wheel` 分支，`consumed` 保持 `false`，宿主按默认行为滚动时间轴。

> 注：早期 `plugins/builtin/note_chain_assist/README.md` 曾记录「Mouse wheel: rotate denominator sequence style（Cycle Density）」，但该交互已演化为右键菜单的「密度选择」（`set_segment_density_*`），参见 `docs/history.md`。当前 Python 实现**无滚轮消费逻辑**。

### 5.5 键盘按下（key_down）

| 键                         | 条件                         | 行为                                                                          |
| -------------------------- | ---------------------------- | ----------------------------------------------------------------------------- |
| `Shift`                  | —                           | 置`shift_down=true`                                                         |
| `A`                      | 无 Ctrl 且宿主不控制锚点模式 | 切换`anchor_placement_enabled`（UI-only，不产生 undo checkpoint）           |
| `Delete` / `Backspace` | —                           | `disconnect_selected_segments` + `delete_selected_anchors`，记 checkpoint |
| `Esc`                    | 有选中                       | 清空锚点/段选择                                                               |

> 键常量：`KEY_A=0x41`，`KEY_DELETE=0x01000007`，`KEY_BACKSPACE=0x01000003`，`KEY_SHIFT=0x01000020`，`KEY_ESCAPE=0x01000000`。

### 5.6 键盘释放（key_up）

`Shift` 释放 → 置 `shift_down=false`。

### 5.7 取消（cancel）

清空 `drag` 与 `link_drag`，`consumed=true`，status=「交互已取消」。

### 5.8 事件消费补充策略（note 选择）

`apply_note_selection_consume_policy`：当 `selection_targets.notes=false`（note 不可选中）时——左键 mouse_down/mouse_up 未消费则强制 `consumed=true`；mouse_move 带按键未消费也强制消费，避免宿主误选 note。

## 6. 运行时状态树（STATE）

全局单例 `STATE`（`modular/core/state.py::build_initial_state`）。C++ 内部实现若要保持对等，需覆盖以下字段。

### 6.1 锚点对象结构（核心）

每个锚点（anchor）为 dict：

| 字段       | 类型                      | 含义                            |
| ---------- | ------------------------- | ------------------------------- |
| `id`     | int                       | 稳定锚点 id（>0）               |
| `lane_x` | float                     | chart 坐标，`[0..lane_width]` |
| `beat`   | float                     | 时间线 beat                     |
| `in`     | `[lane_dx, beat_delta]` | 入侧手柄相对锚点的偏移          |
| `out`    | `[lane_dx, beat_delta]` | 出侧手柄相对锚点的偏移          |
| `smooth` | bool                      | 平滑（对称镜像手柄）或折角      |

> 手柄是**相对偏移**；绝对 chart 坐标 = `anchor.lane_x + in[0]`、`anchor.beat + in[1]`。`smooth=true` 时 in/out 互为相反数（镜像）。

### 6.2 STATE 字段清单

| 字段                                                                                                                           | 初值                                                              | 说明                                             |
| ------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------- | ------------------------------------------------ |
| `anchors`                                                                                                                    | `[]`                                                            | 锚点数组（chart 空间）                           |
| `links`                                                                                                                      | `[]`                                                            | 链接数组，元素`[id0, id1]`（已归一化 min,max） |
| `drag`                                                                                                                       | `{mode:"",index:-1}`                                            | 拖拽态，mode ∈ `""                              |
| `selected_anchor_ids`                                                                                                        | `[]`                                                            | 选中锚点 id                                      |
| `selected_links`                                                                                                             | `[]`                                                            | 选中段（归一化`[id0,id1]`）                    |
| `selection_targets`                                                                                                          | `{anchors:true,segments:true,notes:false}`                      | 三类选择目标开关                                 |
| `segment_denominators`                                                                                                       | `{}`                                                            | 段固定分母，key=`"id0:id1"`                    |
| `segment_shapes`                                                                                                             | `{}`                                                            | 段形状，key=`"id0:id1"`，值 `curve             |
| `context_menu_links`                                                                                                         | `[]`                                                            | 右键命中目标段                                   |
| `box_select`                                                                                                                 | `{active:false,start:[0,0],end:[0,0],append:false}`             | 框选状态                                         |
| `pending_connect_anchor_id`                                                                                                  | `-1`                                                            | 待连接锚点                                       |
| `next_anchor_id`                                                                                                             | `1`                                                             | 锚点 id 分配器                                   |
| `last_click_anchor` / `last_click_ms`                                                                                      | `-1` / `0`                                                    | 双击检测                                         |
| `style`                                                                                                                      | `{denominators:[4,8,12,16],style_name:"balanced"}`              | 分母序列样式                                     |
| `curve_visible`                                                                                                              | `true`                                                          | 曲线 overlay 可见                                |
| `active_link_shape`                                                                                                          | `"curve"`                                                       | 新链接默认形状                                   |
| `note_curve_snap_enabled`                                                                                                    | `false`                                                         | note 吸附曲线开关                                |
| `anchor_placement_enabled`                                                                                                   | `false`                                                         | 锚点放置工具态                                   |
| `project_path`                                                                                                               | `""`                                                            | sidecar 工程路径                                 |
| `project_dirty`                                                                                                              | `false`                                                         | 工程脏标记                                       |
| `history` / `history_index`                                                                                                | `[]` / `-1`                                                   | 插件内部撤销栈（上限 128）                       |
| `curve_revision` / `curve_samples_cache`                                                                                   | `0` / `{}`                                                    | 曲线采样缓存（revision 递增即失效）              |
| `suppress_persist_once`                                                                                                      | `false`                                                         | 丢弃变更后抑制一次持久化                         |
| `link_drag`                                                                                                                  | `{active:false,source_anchor_id:-1,hover_anchor_id:-1,x:0,y:0}` | link 拖拽态                                      |
| `shift_down`                                                                                                                 | `false`                                                         | Shift 状态缓存                                   |
| `last_host_selected_note_ids`                                                                                                | `[]`                                                            | 宿主上次选中 note（用于同步锚点选中）            |
| `anchor_group_ids` / `anchor_reserved` / `anchor_compat_handles`                                                         | `{}`                                                            | 锚点 V3 分组/保留/兼容手柄                       |
| `curve_id_by_link` / `curve_no_by_link`                                                                                    | `{}`                                                            | 曲线稳定 id / 业务编号                           |
| `curve_group_ids_by_link` / `curve_density_mode_by_link` / `curve_reserved_by_link` / `curve_special_joystick_by_link` | `{}`                                                            | 曲线分组 / 密度模式 / 保留 / 特殊手柄            |
| `node_groups`                                                                                                                | `[{group_id:1,group_name:"base",reserved:{}}]`                  | 节点分组                                         |
| `curve_groups`                                                                                                               | `[{group_id:1,group_name:"base",reserved:{}}]`                  | 曲线分组                                         |
| `next_curve_id` / `next_group_id`                                                                                          | `1` / `2`                                                     | id 分配器                                        |
| `project_revision`                                                                                                           | `0`                                                             | sidecar revision（CAS）                          |
| `project_file_uuid`                                                                                                          | `""`                                                            | 工程文件 UUID                                    |
| `project_last_writer_instance`                                                                                               | `""`                                                            | 最后写入实例                                     |
| `project_load_failed`                                                                                                        | `false`                                                         | 加载失败（阻止保存覆盖）                         |
| `last_save_error` / `last_save_error_detail`                                                                               | `""`                                                            | 最近保存错误                                     |
| `host_undo_action_tokens`                                                                                                    | `[]`                                                            | 注册到宿主的 undo 动作标题（上限 64）            |
| `instance_id`                                                                                                                | `f"{pid}-{uuid12}"`                                             | 实例标识（写入 sidecar）                         |

### 6.3 常量

| 常量                                                   | 值                                                                           |
| ------------------------------------------------------ | ---------------------------------------------------------------------------- |
| `LEFT_BUTTON` / `RIGHT_BUTTON`                     | `1` / `2`                                                                |
| `CTRL_MODIFIER_MASK` / `SHIFT_MODIFIER_MASK`       | `0x04000000` / `0x02000000`                                              |
| `KEY_A` / `KEY_DELETE` / `KEY_BACKSPACE`         | `0x41` / `0x01000007` / `0x01000003`                                   |
| `MAX_HISTORY`                                        | `128`                                                                      |
| `SERIALIZE_DEN`                                      | `288`                                                                      |
| `CURVE_CHECKPOINT_PREFIX`                            | `"Plugin Curve Edit"`                                                      |
| `CURVE_SIDECAR_FORMAT_VERSION`                       | `3`                                                                        |
| `DEFAULT_NODE_GROUP_ID` / `DEFAULT_CURVE_GROUP_ID` | `1` / `1`                                                                |
| `STYLE_PRESETS`                                      | `[4,8,12,16]`, `[8,8,12,16,24]`, `[4,6,8,12,16,24]`, `[12,16,24,32]` |

---

## 7. 坐标与时间数学

出处：`modular/core/time_math.py`。

### 7.1 chart ↔ canvas 转换

- **chart 空间**：`lane_x ∈ [0..lane_width]`，`beat` 为时间线拍（float）。
- **canvas 空间**：像素 `(x, y)`，受 `left_margin` / `right_margin`、`scroll_beat`、`visible_beat_range`、`vertical_flip`、`canvas_width/height` 影响。

`canvas_to_chart(x, y)`：

```
available = canvas_width - left_margin - right_margin
lane_x = ((x - left) / available) * lane_width            # clamp 到 [0, lane_width]
t = 1 - y/ch  (vertical_flip) 或  y/ch
beat = scroll_beat + t * visible_beat_range
```

`chart_to_canvas(lane_x, beat)` 为逆变换。

> 宿主上下文若未提供 margin 字段，回退到 `lane_left`/`lane_right`。

### 7.2 triplet 节拍表示

note 的 `beat` 使用 chart-like triplet `[beat_num, num, den]`：

- `triplet_to_float(tri)` = `beat_num + num/den`
- `float_to_triplet(beat, den)`：floor 出整数拍 + 分数部分 round 到 `den` 并约分。
- `SERIALIZE_DEN = 288`（sidecar 序列化统一分母）。
- `beat_fraction_from_triplet` 用 `fractions.Fraction` 精确比较（去重用）。

### 7.3 吸附规则

`snap_chart_point(lane_x, beat, snap_beat, snap_lane)`：

- `snap_lane` 且 `grid_snap`：lane_x 吸附到 `grid_division` 网格。
- `snap_beat`：beat 吸附到 `1/time_division`。

### 7.4 安全分母集合

`safe_denominators`（宿主传入，默认）：
`{1,2,3,4,6,8,12,16,24,32,48,64,96,192,288}`。`_sanitize_denominators` 过滤掉不在安全集合内的分母；过滤后为空则回退 `[4,8,12,16]`。

## 8. 曲线模型与采样

出处：`note_chain_assist.py`（锚点/手柄/采样函数）、`modular/core/curve_model.py`（链接/密度/形状）、`modular/core/time_math.py`（`cubic_point`）。

### 8.1 链接（link）

- 链接是**两个锚点 id** 的无向对，统一归一化为 `(min_id, max_id)`。
- 存储：`STATE["links"]` 为 `[id0, id1]` 数组。
- 链接键 `link_key = f"{min_id}:{max_id}"`，用于查 `segment_denominators` / `segment_shapes` / 各 curve 元数据映射。

### 8.2 段形状（segment shape）

- `segment_shapes[key]` ∈ `curve | polyline`，缺省跟随 `active_link_shape`（默认 `curve`）。
- `curve` = 三次贝塞尔（Bezier）；`polyline` = 直线段（线性插值）。

### 8.3 三次贝塞尔（`cubic_point`）

对段 `(a0, a1)`，四个控制点：

```
p0 = (a0.lane_x, a0.beat)
p3 = (a1.lane_x, a1.beat)
p1 = a0 的出侧手柄绝对坐标 = (a0.lane_x + a0.out[0], a0.beat + a0.out[1])
p2 = a1 的入侧手柄绝对坐标 = (a1.lane_x + a1.in[0],  a1.beat + a1.in[1])
```

标准三次贝塞尔插值：

```
B(t) = (1-t)^3·p0 + 3(1-t)^2·t·p1 + 3(1-t)·t^2·p2 + t^3·p3
```

### 8.4 手柄语义

- **相对偏移**存储：`in` / `out` 为 `[lane_dx, beat_delta]`。
- `smooth=true`：`in` 与 `out` 互为相反数（镜像对称），编辑任一侧自动镜像另一侧（`mirror=True`）。
- `smooth=false`（corner）：in/out 独立。
- 时间约束：手柄 beat 偏移受 `enforce_handle_time_constraints` 约束；锚点移动时 `enforce_anchor_and_connected_handle_constraints` 维持锚点时间顺序与连接手柄一致性。

### 8.5 采样

- `_sample_segment_chart(a0, a1, samples_per_segment, id0, id1)`：对单段按 `samples_per_segment` 步进（渲染默认 `24`，提交默认 `32`）。
- `_sample_curve_chart(samples_per_segment)`：对所有 `links` 逐段采样并拼接（相邻段去掉首点去重）。
- 结果缓存在 `STATE["curve_samples_cache"]`，由 `curve_revision` 递增失效。

### 8.6 段密度（denominator）

- `segment_denominators[key]`：段固定分母 N（生成 note 时步进 `1/N` beat）。
- `curve_density_mode_by_link[key]` ∈ `follow | fixed`；`follow` 时忽略固定值，回退到 `_context_default_segment_denominator`（优先级：`plugin_time_division_override` > 宿主 `time_division` > style.denominators 首项 > 4）。

---

## 9. 批处理提交（buildBatchEdit）

出处：`modular/actions/batch_commit.py`。宿主请求 `buildBatchEdit`（`host_batch_edit` 能力）时，插件返回一次性批处理谱面编辑，宿主将其作为**单个 undo 步**应用。

### 9.1 入口

`build_batch_edit(payload)`：

- 校验 `action_id ∈ {commit_curve_to_notes, commit_curve_to_notes_sidebar, commit_context_segments_to_notes}`，否则返回空编辑。
- `commit_context_segments_to_notes` → 只处理右键目标段（`context_menu_target_links`）。
- 其余 → 处理整条曲线。

### 9.2 曲线 → note 完整流程（`build_batch_from_curve`）

1. **枚举段**：`connected_anchor_segments()` 返回所有 `(i0, i1, id0, id1, a0, a1)`，按 `(min(i0,i1), max(i0,i1))` 排序。
2. **过滤**：若指定 `target_links`，仅保留命中的段。
3. **默认分母**：`plugin_time_division_override` > 宿主 `time_division` > `4`。
4. **逐段生成**：
   - `seg_den = segment_denominator_for_link(id0, id1, default_den)`。
   - 采样 32 点 → 归一化为按 beat 单调序列 `samples_by_beat`。
   - 计算段 beat 范围 `[lo, hi]`，按 `1/seg_den` 步进 `tick`：
     - `beat = tick / seg_den`（落在 `[lo,hi]` 内）。
     - `lane_x = lane_x_at_beat(samples_by_beat, beat)`（在采样点间线性插值）。
     - 生成 note：`{"beat": [beat_num, num, den], "x": int(round(lane_x)), "type": 0}`（`type=0` 即 NORMAL）。
5. **去重**：`existing_normal_note_position_keys`（宿主 `existing_note_positions`，或回退读取 chart JSON 的 `note[]`）+ `seen` 集合，按 `(Fraction(beat), x)` 键去重。
6. **排序**：按 `(beat_num, num, den, x)` 排序。
7. 返回 `{"add": [...], "remove": [], "move": []}`。

### 9.3 note 构造（`chart_to_note`）

```python
def chart_to_note(context, lane_x, beat, den):
    lane_x = clamp(lane_x, 0, lane_width)      # clamp 到 [0, lane_width]
    b, n, d = float_beat_to_triplet(beat, den)
    return {"beat": [b, n, d], "x": int(round(lane_x)), "type": 0}
```

`float_beat_to_triplet`：`ticks = round(beat*den)`；`beat_num = ticks // den`；`num = ticks % den`。

### 9.4 宿主侧解析约束（`ExternalProcessPlugin::parseNoteJson`）

宿主解析插件返回 note 时施加的校验（实现对齐参考）：

- `beat` 三元组：`abs(beatNum) ≤ 1000000`，分母 `1..8192`。
- `x`：整数，宿主按 lane 语义落点。
- `type`：0 = NORMAL（仅 NORMAL note 参与曲线生成）。

## 10. sidecar V3 持久化格式

出处：`note_chain_assist.py`（`_build_v3_payload` / `_serialize_node_for_v3` / `_serialize_curve_for_v3` / `_load_project_v3_payload`）、`modular/core/sidecar_v3.py`（`save_project` / `load_project`）。

### 10.1 文件位置与关联

- 曲线工程文件：`<chart目录>/.mcce-plugin/<chart_stem>.curve_tbd.json`。
- 与谱面 1:1 关联：保存谱面时 `_sync_project_sidecar_to_chart` 把当前 sidecar 复制到该谱面的 `.mcce-plugin/` 目录。
- 打开新工程时 `_try_seed_curve_project_from_source` 可从源谱面复制既有 sidecar 作为种子。

### 10.2 顶层结构

```json
{
  "format_version": 3,
  "coordinate_space": "chart",
  "revision": 1,
  "file_uuid": "<uuid hex>",
  "updated_at": 1713512345678,
  "last_writer_instance": "<pid>-<uuid12>",
  "nodes": [],
  "curves": [],
  "node_groups": [],
  "curve_groups": [],
  "style": { "denominators": [4, 8, 12, 16], "style_name": "balanced" },
  "active_link_shape": "curve",
  "note_curve_snap_enabled": false
}
```

### 10.3 node（锚点）结构

```json
{
  "node_id": 1,
  "lane_x": 256.0,
  "beat": [0, 1, 4],
  "joystick": { "lane_dx": 16.0, "beat_delta": [0, 0, 288] },
  "group_ids": [1],
  "reserved": {},
  "compat_handles": {
    "in":  { "lane_dx": -16.0, "beat_delta": [0, 0, 288] },
    "out": { "lane_dx": 16.0,  "beat_delta": [0, 0, 288] }
  },
  "smooth": true
}
```

- `beat` 为 chart-like triplet `[beat_num, num, den]`。
- `joystick` 是**自动对称模式的唯一手柄来源**（保存出侧手柄）；`compat_handles` 为兼容/未来双手柄编辑保留。
- `lane_dx` / `beat_delta`：`lane_dx` 为数值，`beat_delta` 为 triplet。

### 10.4 curve（链接/段）结构

```json
{
  "curve_id": 1,
  "curve_no": 1,
  "node_ids": [1, 2],
  "density": { "mode": "fixed", "denominator": 4 },
  "style_category": "curve",
  "group_ids": [1],
  "special_joystick_reserved": {},
  "reserved": {}
}
```

- `density.mode` ∈ `follow | fixed`；`fixed` 附带 `denominator`。
- `style_category` ∈ `curve | polyline`。
- `curve_id` 为稳定身份（跨会话），`curve_no` 为业务连续编号（1 起）。

### 10.5 revision CAS 与原子写入

`save_project` 流程：

1. 读取磁盘 sidecar（若存在）取 `disk_revision`。
2. 若 `disk_revision != state.project_revision` → 拒绝保存，报 `revision_conflict`（另一实例已更新，需刷新）。
3. `payload.revision = disk_revision + 1`，写 `updated_at`、`last_writer_instance`。
4. 写临时文件 `{path}.tmp.{pid}.{ms}`，再 `os.replace(tmp, path)` **原子替换**（避免 C++ 侧 audit 指出的"先删后改"数据丢失风险）。
5. 成功后更新 `state.project_revision / project_file_uuid / project_last_writer_instance`，清 `project_dirty`。

### 10.6 加载与版本兼容

- `format_version >= 3` 或同时存在 `nodes` + `curves` → V3 加载。
- 否则 → V2 加载（`anchors`/`links` 扁平结构，兼容旧版）。
- 加载失败时：若磁盘文件存在，置 `project_load_failed=true` 并**阻止保存**（保护原文件）；若文件不存在，则初始化为空工程并标记脏。

---

## 11. 撤销 / 重做模型

### 11.1 插件内部历史（快照式）

- `history` 栈上限 `MAX_HISTORY = 128`。
- `capture_snapshot` 捕获：`anchors / links / style / segment_denominators / segment_shapes / anchor_group_ids / anchor_reserved / anchor_compat_handles / curve_*_by_link / node_groups / curve_groups / selection_targets / curve_visible / active_link_shape / note_curve_snap_enabled / anchor_placement_enabled / pending_connect_anchor_id / next_*_id` 等。
- `push_history`：与栈顶相同则跳过（去重）。
- `undo_history` / `redo_history`：移动 `history_index` 并 `restore_snapshot`，随后 `mark_dirty(flush=false)`。

### 11.2 宿主 undo 集成

- checkpoint 前缀 `CURVE_CHECKPOINT_PREFIX = "Plugin Curve Edit"`。
- 拖拽/框选/link 连接等交互在 `mouse_up` 返回 `request_undo_checkpoint=true`，宿主据此建立一个 undo 检查点。
- 需要与宿主 undo 栈联动的动作（`reset_curve` / `connect_selected_nodes` / `disconnect_selected_segments`）调用 `register_host_undo_action` 把动作标题记入 `host_undo_action_tokens`。
- 宿主 `onHostUndo` / `onHostRedo` 通知到来时，若动作文本以 checkpoint 前缀开头，或匹配已注册 token → 触发插件内部 `undo_history_from_host` / `redo_history_from_host`。

### 11.3 生命周期通知联动

- `initialize`：设置语言。
- `onChartSaved`：解除 `suppress_persist_once`，若工程脏则保存，并 `sync_sidecar_to_chart`。
- `onHostDiscardChanges`：置 `suppress_persist_once=true`、清脏标记。
- `shutdown`：若未抑制且工程脏则保存后退出循环。

---

## 12. i18n 本地化

出处：`modular/core/i18n.py`、`note_chain_assist.py::TRANSLATIONS`。

- 支持语言：`zh` / `en` / `ja`（`normalize_lang` 前缀匹配）。
- 语言检测优先级（`detect_lang`）：context 的 `locale` → `language` → `STATE.lang` → 环境 `MALODY_LOCALE` → `MALODY_LANGUAGE` → 系统 locale → 默认 `en`。
- 翻译表 `TRANSLATIONS` 为 `{key: {en, zh, ja}}` 结构；`tr(context, key, **kwargs)` 按语言取文本并 `format(**kwargs)`。
- 宿主清单还支持 `localizedDisplayName` / `localizedDescription`（zh_CN / ja_JP）。

## 13. 协议方法清单

协议层：宿主 `QProcess` 启动插件，stdin/stdout 单行 JSON（UTF-8）。宿主侧强制 `PYTHONUTF8=1` / `PYTHONIOENCODING=utf-8` / `LC_ALL=C.UTF-8`。

### 13.1 请求方法（request → response）

| 方法                        | 请求 payload                                                                          | 响应 result                                                                                                      | 宿主超时 |
| --------------------------- | ------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | -------- |
| `listToolActions`         | `{}`                                                                                | action 对象数组                                                                                                  | 500ms    |
| `runToolAction`           | `{action_id, context}`                                                              | `bool`                                                                                                         | 15000ms  |
| `listCanvasOverlays`      | `{canvas_width, canvas_height, ...}`（实际宿主传空对象，插件回退 `last_context`） | overlay 对象数组                                                                                                 | 80ms     |
| `handleCanvasInput`       | `{context, event}`                                                                  | `{consumed, overlay, preview_batch_edit, cursor, status_text, request_undo_checkpoint, undo_checkpoint_label}` | 150ms    |
| `getPanelWorkspaceConfig` | context                                                                               | workspace 对象                                                                                                   | 3000ms   |
| `buildBatchEdit`          | `{action_id, context}`                                                              | `{add, remove, move}`                                                                                          | 8000ms   |

### 13.2 通知事件（notify → 无响应）

| event                    | payload                                   | 插件处理                          |
| ------------------------ | ----------------------------------------- | --------------------------------- |
| `initialize`           | `{plugin_id, locale, host_api_version}` | 设置语言                          |
| `onChartChanged`       | —                                        | （忽略）                          |
| `onChartLoaded`        | `{chart_path}`                          | （忽略）                          |
| `onChartSaved`         | `{chart_path}`                          | 解除抑制、按需保存、同步 sidecar  |
| `onHostUndo`           | `{action_text}`                         | 命中 checkpoint/token → 内部撤销 |
| `onHostRedo`           | `{action_text}`                         | 命中 checkpoint/token → 内部重做 |
| `onHostDiscardChanges` | —                                        | 抑制持久化、清脏标记              |
| `shutdown`             | —                                        | 按需保存后退出循环                |

### 13.3 一次性模式（one-shot）

宿主以 `--run-tool-action <action_id>` 启动时（`runToolActionOneShot`），插件 `run_one_shot(action_id)` 直接返回退出码（0=已知动作，1=未知），环境变量导出 `MALODY_LOCALE` / `MALODY_LANGUAGE`。

### 13.4 主循环框架（`modular/runtime/plugin_loop.py`）

- 逐行读 stdin → 解析 JSON。
- `notify`：分派到各生命周期 handler，`shutdown` 时 break。
- `request`：按 `method` 分派到 `list_tool_actions` / `run_tool_action` / `build_overlay` / `handle_canvas_input` / `workspace_config` / `build_batch_edit`；未知 method 返回 `false`。
- 所有异常捕获后写 stderr，并返回 `false`（不崩溃）。
- 响应写 stdout：`{"type":"response","id":...,"result":...}`。

---

## 14. C++ 内部实现对等检查清单

> 用于核对 `src/editor/NoteChain/` 与 `ChartCanvas` 集成是否完整、一致。逐项勾选 `[ ]`。

### 14.1 插件清单与能力

- [ ] 声明与 Python 相同的能力集合（tool_actions / canvas_overlay / canvas_interaction / panel_workspace / host_batch_edit）
- [ ] `pluginApiVersion` 语义对齐（v3 支持交互）

### 14.2 锚点管理

- [ ] 锚点用 **chart 坐标**（lane_x + beat），非 canvas 像素
- [ ] 创建锚点（含顺序插入 + 手柄初值推断）
- [ ] 删除锚点（含级联断开链接）
- [ ] 移动锚点（snap_beat=true、snap_lane=false）
- [ ] 双击平滑/折角切换（280ms 内双击）
- [ ] 锚点选中/多选（Ctrl 切换、Shift link-drag）

### 14.3 手柄（in/out）

- [ ] 拖拽 in/out 手柄
- [ ] smooth 镜像对称、corner 独立
- [ ] 手柄时间约束、锚点时间顺序约束

### 14.4 链接与段

- [ ] 链接归一化（min,max）
- [ ] 连接选中锚点（按索引顺序两两连接）
- [ ] 断开选中段
- [ ] link 拖拽连接（Shift）
- [ ] 段形状 curve/polyline 切换

### 14.5 曲线采样与渲染

- [ ] 三次贝塞尔 + 折线双模式
- [ ] 每段采样（渲染 24 / 提交 32）
- [ ] overlay 元素：预览线/采样点/控制点/手柄/标签/框选矩形
- [ ] overlay 开关（preview/control_points/handles/sample_points/labels）
- [ ] chart-space 坐标渲染（随滚动/缩放/翻转正确）

### 14.6 选择

- [ ] 三类选择目标开关（anchors/segments/notes）
- [ ] 框选（Ctrl/select 模式）
- [ ] 段命中检测（点到折线距离）
- [ ] 锚点/手柄命中检测
- [ ] 从宿主 note 选中同步锚点选中

### 14.7 键盘/手势

- [ ] `A` 切换锚点放置（无 Ctrl、宿主不控制时）
- [ ] `Delete`/`Backspace` 删除选中
- [ ] `Esc` 清空选择
- [ ] 滚轮事件不消费（透传宿主滚动，插件无 wheel 分支）
- [ ] 事件消费策略（note 不可选中时强制消费）

### 14.8 提交（curve → note）

- [ ] 按段密度 `1/den` 步进生成 note
- [ ] lane_x 在采样点间插值
- [ ] note 构造 `{beat:[b,n,d], x:int, type:0}`
- [ ] 去重（已有 note + 段内 seen）
- [ ] 排序（beat, x）
- [ ] 单 undo 步提交
- [ ] 右键段局部提交

### 14.9 撤销/重做

- [ ] 插件内部历史（快照、上限 128、去重）
- [ ] 与宿主 undo 集成（checkpoint 前缀、注册动作 token、onHostUndo/Redo）

### 14.10 sidecar V3 持久化

- [ ] 顶层字段齐全（format_version/coordinate_space/revision/file_uuid/...）
- [ ] node/curve 字段齐全（含 joystick/compat_handles/density/style_category/group_ids）
- [ ] beat 用真实三元 triplet（非 `[v,0,0,288]` 四元）
- [ ] revision CAS 冲突检测
- [ ] 原子写入（tmp + replace，非先删后改）
- [ ] 加载失败阻止覆盖
- [ ] V2 兼容加载
- [ ] 与 chart sidecar 同步（`.mcce-plugin/<stem>.curve_tbd.json`）

### 14.11 样式

- [ ] 分母序列（safe_denominators 过滤）
- [ ] 段密度 fixed/follow
- [ ] 样式导出/导入（`.nca_style.json`）

### 14.12 i18n

- [ ] zh/en/ja 三语言
- [ ] 语言检测优先级一致

### 14.13 性能/健壮性（Python 行为约束）

- [ ] `handleCanvasInput` 响应不含全量 overlay（宿主失效缓存后由 `listCanvasOverlays` 拉取）
- [ ] mouse_move 节流、拖拽时不每帧全量重采样
- [ ] 协议超时降级、未知 method 返回 false 不崩溃

---

## 15. 附录：文件对应与已知差异

### 15.1 Python ↔ C++ 文件对应

| Python 源文件                       | 责任                     | C++ 对应（`src/editor/NoteChain/`）                     |
| ----------------------------------- | ------------------------ | --------------------------------------------------------- |
| `note_chain_assist.py`            | 主入口、核心函数、序列化 | `NoteChainEditor.h/.cpp`                                |
| `modular/core/state.py`           | 状态树、历史             | `NoteChainState.h/.cpp`、`NoteChainHistory.cpp`       |
| `modular/core/time_math.py`       | 坐标/时间数学            | （坐标转换需在 ChartCanvas 层对齐）                       |
| `modular/core/curve_model.py`     | 链接/密度/形状           | `NoteChainCurveSampler.cpp`                             |
| `modular/core/sidecar_v3.py`      | sidecar I/O              | `NoteChainPersistence.cpp`                              |
| `modular/actions/batch_commit.py` | 曲线→note 提交          | `NoteChainCurveSampler.cpp` + ChartController 集成      |
| `modular/actions/tool_actions.py` | 动作清单/分发            | PluginInterface::toolActions / runToolAction              |
| `modular/ui/overlay.py`           | overlay 构建             | `NoteChainOverlay.cpp`                                  |
| `modular/ui/input_handler.py`     | 画布交互                 | `ChartCanvasNoteChain.cpp`、`NoteChainCanvasBridge.h` |
| `modular/runtime/plugin_loop.py`  | 协议循环                 | （迁移后不再需要）                                        |

### 15.2 已知差异 / 风险（来自 audit 文档）

C++ 内部实现当前与 Python 基准的主要差异（详见 `docs/NOTE_CHAIN_ASSIST_CURVE_PLUGIN_AUDIT.md`）：

- **坐标体系**：C++ 侧锚点使用 canvas 像素，但 sidecar 声明 `chart`（Python 侧严格用 chart 坐标）。
- **提交链路**：C++ 侧 `generateNotes()` 只返回采样点，未转成真实 note 接入 ChartController。
- **sidecar 原子写**：C++ 侧"先删后 rename"有丢文件风险（Python 用 tmp + replace）。
- **triplet 语义**：C++ 侧写出 4 元 `[v,0,0,288]`，与 Python 三元 triplet 不一致。
- **revision CAS**：C++ 侧只递增不冲突检测。
- **历史清空**：C++ 侧 `setActive(true)` 每次清历史（Python 侧仅加载新工程时清）。
- **密度未生效**：C++ 侧 segment density 已持久化但采样/生成未使用。
- **未迁移功能**：双击 smooth/corner、段选择、框选等。

---

> 本文档所有行为均可回溯到 Python 源码；若发现 Python 实现后续变更，请同步更新本基准文档。
> 部分参考文档可能过时，请参照源代码。
