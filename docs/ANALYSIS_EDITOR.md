# Analysis Editor — first implementation

Analysis Editor is a second top-level CCE workspace, opened from **View → Analysis Editor**. It edits the same `ChartController` as the Catch workspace. The Catch canvas keeps responsibility for x layout, flow and playability.

## Existing architecture and integration points

| Existing subsystem | Reused integration |
| --- | --- |
| `ChartCanvas::chartYToBeat`, `chartBeatToY`, `setScrollPos` | Optional normalized visible-range synchronization, shared paused time and vertical flip |
| `MathUtils::buildBpmTimeCache` | Audio time ↔ chart beat conversion across BPM changes, including chart offset |
| `ChartCanvas::timeDivision` and `MathUtils::snapNoteToTimeWithBoundary` | Existing snap subdivision and boundary behavior |
| `Note`, `ChartController::addNote/removeNote/moveNote` | Note/Rain creation, deletion and valid Rain tail changes on the existing undo stack |
| `PlaybackController::positionChanged`, `playbackFrameTick`, `seekTo` | Both windows observe one playback clock; the main canvas retains frame-paint acknowledgement ownership |
| `LongRangeSelector` | One shared time range, bidirectional updates from either canvas |
| `PlaybackController::setLoopRange` | One loop range, enforced against both audio observations and predicted display time; rendered in both canvases |
| `Chart::audioSourceFullPath`, metadata audio path | The same project audio, with relative-file fallback |
| `BpmDetector::analyzeFromFileDetailedAsync` | Existing asynchronous legacy + AutoTiming 2 pipeline, decoded once per timing job |
| `AutoTiming2Bridge` DTOs | Diagnostic projection preserving whole-file audio positions, stage status, confidence and uncertainty |
| `CommandRouter` / `Settings` shortcut overrides | Shared undo/redo/playback shortcuts; independent workspace ownership prevents Catch spatial commands leaking into Analysis Editor |
| `ChartCanvas::beginPastePreview` | Existing future candidate-preview boundary; generation is not implemented in this phase |

## Working interface

The core defaults to approximately half the window width, leaving blank space available for optional panels. Drag the splitter to the right of the Note lane to resize the entire core. The Note/Rain lane defaults to 56 logical pixels (two 28-pixel Notes), with a minimum of 42 pixels (1.5 Notes). Drag the divider between Spectrum and Note/Rain to resize those areas independently. The lane keeps its chosen pixel width as the core or window changes size. Side tabs toggle scrollable, resizable panels; clicking an open tab collapses it. Closing the window hides it without closing the project. Geometry, splitters, panel visibility, zoom mode and lane width are persisted separately from the main editor layout.

Spectrum displays separate left and right log-frequency spectrograms. Small gutters display peak amplitude and RMS. Mono is explicitly duplicated to both views. Timing changes appear as dashed lines across both areas with `[BPM]` labels; measure, beat and current subdivision lines share the same canvas. Four beats per measure follows CCE's present grid convention.

The lane displays ordinary Note at its center regardless of stored x. Rain is a filled duration rectangle, with ordinary Notes drawn above it. The compact tool menu at the top switches Note / Rain. Earlier Spectrum fraction settings are replaced by the compact lane default. New objects use x=256; existing objects retain their x. Left click places a snapped Note, or selects an existing object. Right click deletes nearby objects regardless of x. Rain uses two clicks for start/end; right click or Escape cancels an unfinished placement. In Note mode, an existing Rain body leaves its interior available for ordinary Notes. In Rain mode, its body is selectable/deletable, with nearby Note heads taking precedence. Drag an existing Rain tail to change its end with one undo entry. Shift-drag creates the shared time range. Spectrum click, middle click or wheel seeks the shared playback head. Ctrl-wheel zooms independently by default. The optional **Sync visible range** mode follows the main canvas's beat-based viewport; changing its zoom then changes the main canvas scale.

The bottom controls are limited to playback, current time, loop range, undo/redo and a short status. Loop uses the shared selected range; ranges under 10 ms are disabled. Chart loading resets the loop.

## Timing debugging

The Timing tab can analyze a specified audio range, the selection, or the visible interval. It exposes:

- Global BPM candidates, raw estimates, uncertainty, harmonic family support and phase confidence.
- Multiscale windows, reliability, evidence, anchor selection and estimator messages.
- Each selected window's local candidates, including absolute pulse time, phase confidence, origin and uncertainty.
- Tempo track, segments, uncertainty regions and failure reasons.
- Maximum phase-anchor residual and piecewise BPM-list model error.
- Per-anchor signed time residuals, derived by inverting the published normalized cubic phase curve.
- Raw periodicity evidence, plus optional gated semantic subdivision/rhythm profiles.

Double-click a timed result row to seek to it and switch to a close independent view. Global candidates without a pulse location cannot provide a seek target.

**Windows** selection fills **Window candidates**; double-clicking a window opens its candidates and seeks to its start. Candidate details distinguish absolute pulse time from the legacy offset. **Go to pulse** synchronizes both editors to the reported audio position. Tempo-only candidates and empty windows disable phase actions.

**Preview candidate grid** is off by default. It displays cyan constant-BPM references at the selected candidate's absolute pulse, limited to the analyzed window/range. Dense grids are thinned without shifting phase. This is a diagnostic reference, not the fitted variable-tempo model; it leaves chart BPM, offset, placement snap, notes and undo history unchanged. New analysis or source changes clear the preview.

The Diagnostics tab shows and exports full JSON, including nested local candidates, families, hypotheses, objective costs, phase/tempo models, rhythm profiles, support IDs, options and source provenance. `pulseTimeSeconds` is an absolute audio location and is null when absent. `legacyOffsetMilliseconds` retains its separate delay-style meaning. Objective costs and raw scores are not presented as calibrated probabilities. Pulse indices are serialized as decimal strings to preserve integer precision.

These diagnostics never modify the chart or populate a BPM list. Uncertain evidence and unavailable model results remain visible as such.

## Implementation boundaries

`analysis/SpectrumAnalysis` accepts interleaved float PCM and returns plain standard-library data; it has no Qt, widget, playback, project or selection dependency. Its FFT is deterministic radix-2 STFT with a 2048-sample Hann window, 128 log-frequency display bands, a −90 dB floor, and approximately 10 ms hop (increased when needed to bound output to 12,000 frames). At 44.1 kHz the FFT-bin resolution is approximately 21.5 Hz. It is a visual inspection baseline, not an onset classifier.

`audio/SpectrumService` adapts Qt decoding to that core. Each request is limited to 120 seconds and 32 million float samples, with timeout and cooperative cancellation. Only the first two channels of multichannel input are displayed. PCM is cropped in audio time before analysis. Qt's decoder is sequential, so inspecting late parts of long files still decodes/skips the preceding audio; a seekable PCM cache is a future performance improvement. The GUI requests bounded pages covering viewports up to 100 seconds and caches short-file EOF to avoid repeated analysis.

Only one timing worker runs per window. Cancel discards its result and cooperatively stops preparation. The pinned AutoTiming core has no mid-analysis cancellation hook, so a running core calculation must finish before another timing job can start. Source/chart generations and audio file identity reject stale asynchronous results. Hiding the editor cancels spectrum work and discards outstanding timing work.

Transient, Timbre, Tracks and Generation are explicitly marked future panels. The current extension point is only `addAnalysisPanel`; no speculative algorithm framework, machine learning, pseudo tracks or automatic chart generation is introduced. Future audio events can remain plain input/output data. Future chart candidates must enter an explicit ghost preview and then a separate undoable commit before reaching the real chart.

## Validation

`SpectrumAnalysisTests` builds the core without Qt and checks stereo tone separation, audio origin, RMS/peak, mono duplication, silence, invalid samples and cancellation.

`AnalysisEditorTests` uses the real MainWindow/controller integration in Qt's offscreen platform. It checks compact/persistent layout, coordinate inversion and viewport modes, bidirectional seeking, shared undo, isolated shortcuts, Rain tail editing, shared range/loop, diagnostic semantics, window-candidate navigation, absolute-phase preview placement and range limits, unchanged chart/snap during preview, actual stereo WAV decoding, the real asynchronous timing pipeline, stale-result rejection, playback synchronization and loop wraparound.

The normal CCE tests remain enabled. Native Windows/macOS appearance and hardware audio latency require platform validation; the first implementation is validated with Qt 6.4 on Linux.

## Original implementation patch

The original standalone `CCE-Analysis-Editor.patch` is based on the existing CCE iteration commit `2f082668ee23373def420052ee1cdb652a83dc48` (the `codex/cce-iteration-20261003` branch). Apply it to that base or review the local `codex/analysis-editor-20261003` branch. The patch leaves the AutoTimingCore submodule pin unchanged.

```sh
git apply --check CCE-Analysis-Editor.patch
git apply CCE-Analysis-Editor.patch
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Final verification: the application and all test binaries built with Qt 6.4.2 / GCC 13 on Linux. All 15 CTest targets passed across the full regression run and the final focused rerun of the two Analysis targets after UI fixes. Actual offscreen screenshots were inspected for compact default width, full canvas height, timing labels, stereo separation, readable diagnostic text and minimal bottom controls.
