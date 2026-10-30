# Runtime Plugins Directory

This directory is for runtime plugin deployment (non-source).

Current host API: **v3**; API v2-v3 manifests are accepted. Last verified: **2026-09-10**.

## Purpose

- Put plugin binaries/manifests/scripts here for local runs.
- The app normally resolves the plugin directory as `<appDir>/plugins`.
- `MCCE_PLUGINS_DIR` explicitly overrides that path (relative paths are based on
  the executable directory). Local source plugins are a fallback only when the
  runtime directory is missing.
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

## D Catch converter

The Windows CMake build also compiles the D converter from
`third_party/MalodyCatch2CTB-D` and stages it directly into
`<appDir>/plugins/malodycatch2ctb-d/` with its manifest and MIT license.
Install LDC (`ldc2` on PATH) before configuring, or explicitly disable it with
`-DCATCHEDITOR_BUILD_CTB_PLUGIN=OFF`. The built plugin needs no D/Python runtime
installation. It exports the synchronized current chart to `.osz` in
`ctb-output/` beside the original chart. See the source package's
[README](../third_party/MalodyCatch2CTB-D/README.md) for standalone `.mc`/`.mcz`
conversion and manual installation.

`sync_plugins_to_builds.ps1` only synchronizes this runtime source folder;
rebuild the `MalodyCatch2CTBPlugin` CMake target to refresh the compiled converter.
