# Canvas Interaction Protocol (Host API v3)

This document defines an extension for direct plugin interaction on the main chart canvas.

## 1. Purpose

Current `canvas_overlay` is draw-only.  
For pen-like curve editing in the main editor area, plugins also need input events and a way to consume them.

Scope constraint for this plugin family:

- Only operate on regular notes (`NORMAL`).
- Do not mutate chart file format/schema.
- Extra data must be persisted in plugin-owned sidecar files.

Host API v3 adds:

- A new capability: `canvas_interaction`
- New host-to-plugin request: `handleCanvasInput`
- Optional response fields for immediate overlay refresh and preview edits
- Optional workspace capability for dockable multi-window plugin UI: `panel_workspace`

## 2. Versioning

- Host API v2: no interactive canvas events (existing behavior).
- Host API v3: supports `canvas_interaction`.

Compatibility rules:

- Host v3 must still run v2 plugins unchanged.
- Plugin may advertise `pluginApiVersion: 2` and still work without interaction.
- Plugin that requires direct interaction should declare `pluginApiVersion: 3` and capability `canvas_interaction`.

## 3. Manifest

Example:

```json
{
  "pluginId": "demo.note_chain_assist",
  "displayName": "Note Chain Assist",
  "version": "0.1.0",
  "description": "Interactive note-chain shaping tool",
  "author": "Your Name",
  "pluginApiVersion": 3,
  "capabilities": [
    "tool_actions",
    "floating_panel",
    "canvas_overlay",
    "host_batch_edit",
    "canvas_interaction",
    "panel_workspace"
  ],
  "executable": "python",
  "args": ["./plugin.py"]
}
```

## 4. New Request: `handleCanvasInput`

Host sends:

```json
{
  "type": "request",
  "id": "1713512345684",
  "method": "handleCanvasInput",
  "payload": {
    "context": {
      "chart_path": "D:/beatmap/test.mc",
      "canvas_width": 1200,
      "canvas_height": 800,
      "left_margin": 40,
      "right_margin": 20,
      "scroll_beat": 32.0,
      "visible_beat_range": 8.0,
      "vertical_flip": false,
      "time_division": 8,
      "grid_division": 4,
      "lane_width": 512,
      "note_type_scope": "normal_only",
      "selected_note_ids": ["n1", "n2"],
      "selected_notes": [
        {"id":"n1","x":256,"lane_x":256,"beat":32.5},
        {"id":"n2","x":300,"lane_x":300,"beat":33.0}
      ],
      "host_selection_tool": {
        "mode": "select|place_note|place_rain|delete",
        "is_select_mode": true,
        "is_selecting_rect": false,
        "selection_rect": {"x": 0, "y": 0, "w": 0, "h": 0},
        "ctrl_toggle_select": true,
        "empty_click_clears_selection": true,
        "escape_clears_selection": true
      },
      "safe_denominators": [1,2,3,4,6,8,12,16,24,32,48,64,96,192,288]
    },
    "event": {
      "type": "mouse_move",
      "x": 420.5,
      "y": 260.0,
      "button": 0,
      "buttons": 1,
      "modifiers": 0,
      "shift_down": false,
      "ctrl_down": false,
      "wheel_delta": 0.0,
      "key": 0,
      "timestamp_ms": 1713512345684
    }
  }
}
```

## 5. Response Schema

Plugin responds:

```json
{
  "type": "response",
  "id": "1713512345684",
  "result": {
    "consumed": true,
    "overlay": [
      {"kind":"line","x1":10,"y1":10,"x2":100,"y2":100,"color":"#FFAA00","width":2.0}
    ],
    "preview_batch_edit": {
      "add": [],
      "remove": [],
      "move": []
    },
    "cursor": "crosshair",
    "status_text": "Dragging control point #2"
  }
}
```

Fields:

- `consumed` (bool, required): when true, host should stop default canvas handling for this event.
- `overlay` (array, optional): one-shot overlay diff/full snapshot for low-latency update.
- `preview_batch_edit` (object, optional): temporary, non-committed edits for host preview pipeline.
- `cursor` (string, optional): e.g. `arrow`, `crosshair`, `size_all`, `size_hor`.
- `status_text` (string, optional): status bar hint.
- `event.shift_down` (bool, optional but recommended): explicit Shift state from host.
- `event.ctrl_down` (bool, optional but recommended): explicit Ctrl state from host.

If plugin cannot handle the event, it should return:

```json
{"type":"response","id":"...","result":{"consumed":false}}
```

## 6. Event Types

Supported `event.type`:

- `mouse_down`
- `mouse_move`
- `mouse_up`
- `wheel`
- `key_down`
- `key_up`
- `focus_in`
- `focus_out`
- `cancel` (escape-like abort)

## 7. Host Behavior Rules

- Host should only dispatch `handleCanvasInput` to plugins that declare `canvas_interaction`.
- Dispatch only while plugin edit mode is active (avoid stealing default editor behavior).
- If `consumed=true`, host must skip default action for that event.
- For performance, host may throttle `mouse_move` dispatch (recommended: 60-120 Hz max).
- On timeout, host should fallback to default behavior and disable interaction for this session.

Timeout recommendation:

- `handleCanvasInput`: 16-24ms soft target, 50ms hard timeout.

## 8. Native Plugin API (C++ Suggestion)

```cpp
struct CanvasInputEvent
{
    QString type;
    QPointF pos;
    int button = 0;
    int buttons = 0;
    int modifiers = 0;
    bool shiftDown = false;
    bool ctrlDown = false;
    double wheelDelta = 0.0;
    int key = 0;
    qint64 timestampMs = 0;
};

struct CanvasInputResult
{
    bool consumed = false;
    QList<PluginInterface::CanvasOverlayItem> overlay;
    PluginInterface::BatchEdit previewEdit;
    QString cursor;
    QString statusText;
};

virtual bool handleCanvasInput(const QVariantMap &context,
                               const CanvasInputEvent &event,
                               CanvasInputResult *outResult);
```

## 9. Relationship with Existing APIs

- `listCanvasOverlays` remains valid as periodic pull.
- `handleCanvasInput.result.overlay` is optional fast-path push result.
- Final commit should still use `buildBatchEdit` or `runToolAction`.

## 9.1 Optional Workspace Contract (`panel_workspace`)

Purpose:

- Prefer portable/floating controls and reduce fixed toolbar footprint.
- Enable multi-window collaboration with dock/merge behavior.

Suggested host context for panel creation:

```json
{
  "workspace_id": "note_chain_workspace",
  "docking_supported": true,
  "tab_merge_supported": true
}
```

Suggested plugin panel metadata extension:

```json
{
  "panel_id": "color_sequence_panel",
  "title": "Color Sequence",
  "panel_role": "secondary",
  "dock_preference": "right"
}
```

## 10. Safety and Data Integrity

- Preview edits from interaction must not be saved immediately.
- Final write must be explicit (`commit` action).
- Host should keep all plugin commits as single undo steps.
- For denominator/color edits, plugin must preserve exact timing unless user explicitly allows fallback.
- Plugin should ignore non-normal notes by default.
- Sidecar files for curve/session/style data should be versioned and independent of `.mc` schema.
