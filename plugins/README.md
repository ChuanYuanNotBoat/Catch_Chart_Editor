# Runtime Plugins Directory

This directory is for runtime plugin deployment (non-source).

Current host API: **v3**; API v2-v3 manifests are accepted. Last verified: **2026-09-10**.

## Purpose

- Put plugin binaries/manifests/scripts here for local runs.
- The app resolves plugin directory from executable path as `<appDir>/plugins` only.
- Build step copies this repo folder into the executable output directory automatically.
- Install/package step also ships this folder into release output.

## Suggested layout

```text
plugins/
  builtin/
    note_color_formatter/
      note_color_formatter.plugin.json
      note_color_formatter.py
  samples/ (not loaded by default)
    ...
```

## Notes

- Keep `*.plugin.json` and script relative paths aligned.
- `plugins/samples/` is excluded from runtime discovery; copy a sample into a non-sample plugin directory before testing it.
- For Python process plugins, ensure `python` is available in PATH.
- `builtin.note_chain_assist` has been replaced by the native C++ editor in
  `src/editor/NoteChain`; the host deliberately skips that legacy plugin.
- Plugin development documentation starts at `src/plugin/README.md`.
