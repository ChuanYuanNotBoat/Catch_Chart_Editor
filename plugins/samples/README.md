# Runtime Samples

This folder contains runnable sample plugin payloads for quick local verification.

Samples are excluded from normal discovery and are not production plugins. Last verified: 2026-09-10.

- `beat_normalizer/`: legacy Host API v2 process example that directly rewrites a chosen file; use only on copies.
- `note_chain_interaction_demo/`: Host API v3 interactive canvas protocol sample (`canvas_interaction`, `panel_workspace`); the production Note Chain editor is native C++.

For new work, start with the current protocol in
[`src/plugin/docs/PROCESS_PLUGIN_PROTOCOL.md`](../../src/plugin/docs/PROCESS_PLUGIN_PROTOCOL.md).
