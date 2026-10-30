# MalodyCatch2CTB → D + CCE Process Plugin

D language rewrite of [ChuanYuanNotBoat/MalodyCatch2CTB](https://github.com/ChuanYuanNotBoat/MalodyCatch2CTB), built as an **independent standalone converter** and **CCE Process Plugin**.

Imported from the user-supplied `MalodyCatch2CTB-D-CCE-prototype.zip` on 2026-10-31.
Local integration fixes cover LDC const/reference compatibility and CCE working-copy
export placement. Verified with LDC 1.43.0 / DUB 1.42.0 on Windows x64 using the
converter tests and CCE's real plugin loader and asynchronous process adapter.
The original ZIP remains unchanged. This source is distributed under the included MIT license.

## Scope

- Converts a `.mc` or `.mcz` on disk to `.osz`. CCE supplies its synchronized current working copy.
- `.mcz`: enumerates Catch (`mode=3`) charts, supports multiple difficulties, carries audio and image assets, flattens asset names and handles collisions.
- `.mc`: converts the chart using audio/image files from its directory.
- Recreates basic metadata, BPM/red timing points, `meta.offset` or fallback sound-note offset, fruit/spinner-rain hit objects, whole-beat NewCombo, and default CS/AR.
- Uses only D's standard library (`std.json`, `std.zip`) with no Python dependency.
- CCE integration: one `runToolAction` button under **Tools / Plugin Tools**, with JSONL `stdin`/`stdout`; manifest API v2.
- NEVER modifies the original chart or `.mcz`; writes a new file to a `ctb-output/` folder next to source and avoids filename overwrites. In CCE, `chart_path_source` selects the output location while `chart_path` supplies the current working-copy content.

## CCE repository build

On Windows the main CMake build enables `CATCHEDITOR_BUILD_CTB_PLUGIN` by default.
Install LDC and put `ldc2` on PATH before configuring. CMake compiles the plugin,
stages its executable, manifest and MIT license under the host executable's
`plugins/malodycatch2ctb-d/`, and includes them in `cmake --install` output.
No compiler or DUB is required on users' machines to run the resulting plugin.

```powershell
cmake -S . -B build -DCATCHEDITOR_BUILD_CTB_PLUGIN=ON -DBUILD_TESTING=ON
cmake --build build --config Release --target CatchChartEditor CtbProcessPluginTests
ctest --test-dir build -C Release -R 'ctb_converter_smoke|CtbProcessPluginTests' --output-on-failure
```

These commands run from the CCE repository root. Use
`-DCATCHEDITOR_BUILD_CTB_PLUGIN=OFF` for a host-only build. Other desktop platforms
can explicitly enable the option with a native LDC installation; they have not
been verified here. The standalone commands below run from this source directory.

## Build (Windows)

1. Install [LDC](https://github.com/ldc-developers/ldc/releases) plus [DUB](https://dub.pm/) and put `ldc2` and `dub` on `PATH`.
2. From this repository directory:

```powershell
dub build --compiler=ldc2 --build=release
python tests/smoke.py .\malodycatch2ctb-d.exe
```

Or build and install directly:

```powershell
.\scripts\install.ps1 -CceExeDir 'C:\path\to\CatchChartEditor-release'
```

`CceExeDir` must be the directory containing `CatchChartEditor.exe`. Restart CCE or reload plugins and find **导出 osu! Catch (.osz)** among plugin tools. Save a new chart before its first export. CCE synchronizes edits to its working copy before invoking the plugin. Output goes to `ctb-output/` beside the source chart (or beside the input if no source path is supplied); success/failure is returned to the host and technical log messages go to stderr.

**Manual installation:** copy the executable, `LICENSE` and `plugin/malodycatch2ctb-d.plugin.json` side by side into `<CCE executable directory>/plugins/malodycatch2ctb-d/`. CCE recursively discovers manifests and resolves `./malodycatch2ctb-d.exe` relative to the manifest's folder.

## Standalone usage

```powershell
.\malodycatch2ctb-d.exe 'C:\charts\song.mcz'
.\malodycatch2ctb-d.exe 'C:\charts\song.mc' 'C:\charts\out'
.\malodycatch2ctb-d.exe 'C:\charts\song.mcz' --dedupe
```

`--dedupe` is **off by default** (to preserve overlapping notes). It preserves the original Python algorithm's approximate same-time grouping, including its midpoint selection; this is **not equivalent** to "same x only" despite the original README wording.

## Known differences / limitations

1. **UTF-8 (including UTF-8 BOM) supported; GBK fallback is not implemented.** Convert old GBK-encoded `.mc` to UTF-8 first.
2. Outside CCE, conversion reads only the disk copy. CCE synchronizes its working copy before a tool action; export reads this synchronized copy without writing the source.
3. Existing source `.mc` assets are searched only next to the input file. External/relative nested directories are not automatically traversed.
4. Resource-name collision handling updates audio/background references in the generated `.osu`, unlike the Python implementation's collision edge case.
5. ZIP input is validated for unsafe paths and constrained with basic entry/count/size limits; unsupported ZIP compression methods may fail. No files are extracted to disk.
6. Plugin actions use CCE's **15-second** timeout for `runToolAction`; huge packages may time out. For very large packages use CLI mode.
7. One `.mcz` produces one `.osz`; `--dedupe` is a CLI-only option.
8. Does not port the original Python CLI's directory-recursive scanning or automatic archive-to-`input` workflow (neither is appropriate to a one-click CCE plugin).
9. Does not implement preview audio, conversion progress bars or GUI configuration.

## Suggested verification before merging

- Compile with LDC and fix any version-specific API incompatibilities.
- Run `python tests/smoke.py path/to/executable` and compare output files against the original Python converter on representative charts.
- In CCE: test `listToolActions`, a saved `.mc`, a saved `.mcz`, errors for missing audio, and repeated export collisions.
- Test Unicode chart names, multiple difficulty packages, BPM changes and offset edge cases.
- Only then package a Windows executable, write release notes and consider opening a PR.

## Original project and credits

- [MalodyCatch2CTB](https://github.com/ChuanYuanNotBoat/MalodyCatch2CTB) (MIT)
- [CCE Process Plugin Protocol](https://github.com/ChuanYuanNotBoat/Catch_Chart_Editor/blob/main/src/plugin/docs/PROCESS_PLUGIN_PROTOCOL.md)

Prototype code is offered under MIT; retain upstream notices/credit for actual distribution.
