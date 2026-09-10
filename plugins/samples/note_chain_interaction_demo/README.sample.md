# Note Chain Interaction Demo (Process Plugin)

A runnable Host API v3 sample focused on pen-like curve editing:

> Protocol reference only; last verified: 2026-09-10. The authoritative production
> Note Chain implementation is `src/editor/NoteChain/` and does not start this process plugin.

- capabilities: `tool_actions`, `canvas_overlay`, `canvas_interaction`, `panel_workspace`, `host_batch_edit`
- interactive features:
  - left click empty area: add anchor
  - left drag anchor: move anchor
  - drag handle square: adjust Bezier handle
  - double click anchor: smooth/corner toggle
  - right click anchor: delete anchor
  - sidecar auto-save/load via `curve_project_path` context
- action:
  - `Commit Curve -> Notes` (builds one batch edit with normal notes)
  - `Export Style Preset` / `Import Style Preset` (shared style file in style library path)

## Files

- `note_chain_interaction_demo.plugin.json`
- `note_chain_interaction_demo.py`

## Install

1. Copy folder to `{appDir}/plugins/note_chain_interaction_demo/`
2. Ensure `python` is available in PATH
3. Restart editor

## Usage

1. Enable `Tools -> Plugin Enhanced Tool Mode`
2. Edit curve directly in main canvas
3. Use `Tools -> Plugin Actions -> Commit Curve -> Notes` to generate notes
4. Undo once to rollback the whole generated batch
