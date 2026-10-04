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

**Timing → AutoTiming** can analyze a specified audio range, the selection, or the visible interval. It exposes:

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

Timbre, Tracks and Generation are explicitly marked future panels. The current extension point is only `addAnalysisPanel`; no speculative algorithm framework, machine learning, pseudo tracks or automatic chart generation is introduced. Future audio events can remain plain input/output data. Future chart candidates must enter an explicit ghost preview and then a separate undoable commit before reaching the real chart.

## Manual timing measurement

Open **Timing → Measure** and enable **Pick Start → End**. The first spectrum click snaps Start to the Catch editor's current beat subdivision. Clicking an existing timing line (within 5 logical pixels) reuses its original Malody `[whole, numerator, denominator]`, including a non-canonical fraction. Otherwise the triplet is created directly on the subdivision; it is never reconstructed from the displayed start milliseconds. The second click chooses an unsnapped absolute audio End. Drag a marker within 8 pixels to adjust it: Start continues to snap and End stays free. Middle-click still seeks; Shift-drag still selects the shared range. Picking pauses playback. Closing/hiding the panel or switching tabs stops picking; Escape clears the draft. Tab on the spectrum focuses **Beat span**.

The default span is **1 beat**, restored for each cleared/new measurement. Decimal and fractional input such as `0.5`, `1/3` and `4` retain their exact rational span (up to nine decimal places). `BPM = 60000 × span / (End − Start)` uses unrounded measurements; the UI offers numeric End adjustment, full-precision BPM copying and a purple reference grid inside the measured interval. The Start label reports its authoritative beat triplet plus a derived audio time. Preview and dragging leave the chart, snap, selection, offset and undo history unchanged.

**Apply BPM at Start** updates an existing timing at the exact same rational position or adds one new timing point through the shared `ChartController`. Existing triplets, note beat coordinates, offset and later timing points are preserved. The BPM applies until the next timing point and affects later audio times. The panel blocks applying across another timing inside the chosen beat span, before the first chart timing, at duplicate timing positions, or with invalid/reversed input; it also avoids a no-op update. A chart containing notes asks for confirmation, defaulting to No. The edit is one shared undo/redo command. Timing/metadata/source changes clear the draft to prevent applying stale measurements.

Manual markers are only displayed while Measure is visible. AutoTiming's cyan candidate preview remains a separate diagnostic and is disabled when entering Measure. This iteration implements the constant-BPM Measure → Preview → Apply flow; adaptive BPM interpolation remains the next tool. STFT hop and screen zoom limit visual alignment: three decimal places in the End field do not imply matching audio accuracy or a certified song tempo.

`analysis/TimingMeasurement` uses only the standard library. Its standalone tests cover exact decimal/fraction parsing, the default beat, absolute-time BPM calculation and malformed/non-finite/reversed inputs. Editor tests drive real mouse clicks and both marker drags, Tab focus, copy, existing non-canonical timing preservation, crossing rejection, both note-confirmation responses, and shared undo/redo.

## Transient debugging

The Transient tab now uses the decoded stereo spectrum page without a second audio decode. Its **Curves** view shows total, low (30–250 Hz), mid (250–2000 Hz) and high (2000 Hz–Nyquist) positive spectral flux. Time runs vertically and follows the main Analysis canvas's current visible interval, flip direction and playback time. Each colored curve can be hidden independently; the dashed purple threshold applies to the total flux. Click the plot to seek both editors into a close independent view.

**Peaks** lists absolute audio time, disposition, strength, half-height peak width and band strengths. Double-click a peak to seek; rejected peaks can be hidden. Rejection reasons distinguish a weak peak, minimum-interval suppression and the FFT's padded page edges. Peak width describes a flux lobe, not the duration of the underlying sound. Neither parameter changes, seeking nor JSON export writes notes, timing, snap or undo history.

Relative threshold is a fraction of the strongest total flux in the loaded audio page. A 150 ms local median floor is also applied. Minimum interval keeps the stronger of nearby peaks; it does not assume a BPM or quantize event times. Export includes every curve frame, threshold and detected/rejected peak, options and STFT provenance.

`analysis/TransientAnalysis` has no Qt or editor dependency. It rectifies channels separately and averages positive differences of log-compressed linear magnitudes from the existing display-band STFT. This is an inspection baseline, not a calibrated detector: log bands overlap, FFT resolution and hop limit temporal accuracy, and page-relative thresholds can change when a new page is loaded. The first frame has no previous-frame evidence and is not a detected onset; a half-FFT margin excludes page-edge peaks. Absolute audio times never include chart offset.

The panel debounces parameter edits and allows one background transient worker. A new spectrum page, new parameters or source changes cancel/supersede obsolete work. Source changes immediately clear results and export data, and stale callbacks cannot repopulate them. A shared immutable page keeps worker input alive safely after the panel is destroyed.

## Validation

`SpectrumAnalysisTests` builds the core without Qt and checks stereo tone separation, audio origin, RMS/peak, mono duplication, silence, invalid samples and cancellation.

`TransientAnalysisTests` also builds without Qt. Synthetic frequency bursts check band attribution and absolute times; a weaker high-frequency attack verifies threshold recovery. Additional cases check opposite-channel rectification, stronger-peak suppression, padded boundaries, silence, malformed/non-finite input and cancellation. Editor integration checks real WAV-derived events, row filtering, peak/plot seeking, JSON provenance, rapid parameter updates and rejection of an active worker after source replacement.

`AnalysisEditorTests` uses the real MainWindow/controller integration in Qt's offscreen platform. It checks compact/persistent layout, coordinate inversion and viewport modes, bidirectional seeking, shared undo, isolated shortcuts, Rain tail editing, shared range/loop, diagnostic semantics, window-candidate navigation, absolute-phase preview placement and range limits, unchanged chart/snap during preview, actual stereo WAV decoding, the real asynchronous timing pipeline, stale-result rejection, playback synchronization and loop wraparound.

The normal CCE tests remain enabled. Native Windows/macOS appearance and hardware audio latency require platform validation; the first implementation is validated with Qt 6.4 on Linux.

The compact-lane and candidate-preview iteration built with Qt 6.4.2 / GCC 13 on Linux and passed all 15 CTest targets. Its UI checks include returning to the same selected row in another table, empty-window handling and clearing old previews after an audio source change.

The Transient iteration built on the same Linux/Qt environment and passed all 16 CTest targets (127.26 s). After the final JSON/legend/table refinements and active-worker cancellation case, all three Analysis targets passed again (17.55 s). The real WAV/timing screenshot case also passed, and both new Transient views were visually inspected. Native Windows/macOS appearance and real-song detector tuning remain follow-up validation.

To reproduce the five runtime screenshots, run the real audio/timing integration case with an output directory:

```sh
QT_QPA_PLATFORM=offscreen CCE_ANALYSIS_SCREENSHOT_DIR=/tmp/cce-analysis-screenshots \
  ./build/AnalysisEditorTests realAudioAndTimingPipeline
```

The fixture creates 12 seconds of synthetic stereo pulses, decodes and analyzes that WAV through the production pipeline, and temporarily adds Notes and Rain through the chart controller. It captures the collapsed layout, a close view, global timing candidates, a window candidate's grid preview, raw diagnostics, transient curves and the detected/rejected peak table. Fixture chart edits are undone after capture.

### User-supplied audio inspection

The window header now identifies the loaded audio by filename. An optional test case uses the actual project decoding, asynchronous analysis, table interactions and playback controllers to inspect a local audio file. It skips when the environment variable is absent; no music files or generated song diagnostics are included in the repository and no new dependency is required.

```sh
QT_QPA_PLATFORM=offscreen CCE_ANALYSIS_AUDIO_FILE=/absolute/path/music.flac \
  CCE_ANALYSIS_SCREENSHOT_DIR=/tmp/cce-real-audio \
  ./build/AnalysisEditorTests externalAudioInspection
```

The case checks the opening eight seconds, an eight-second range at 40% of the file, a late eight-second range and a cropped EOF range. It validates finite stereo magnitudes, absolute event locations and page boundaries. Equal channels and silent ranges are valid audio, not failures. In the real editor it checks parameter restoration, peak double-click navigation, 16 seconds of timing analysis, local candidate preview, loop wraparound, unchanged chart/undo history before editing, and clearing results after source removal. It then drives manual Start/End clicks on the real spectrum, drags End, verifies `1/3` span scaling, applies one BPM point, and undoes/redoes it before restoring the reference chart. It captures nine views and exports transient/timing/manual JSON plus an inspection report. The candidate preview screenshot selects the available local candidate with the highest reported phase confidence; that selection is only test setup, not an automatic timing recommendation.

On 2026-10-03 the case was run directly on user-supplied **II-L – SPUTNIK-1** (173.647 s) and **Horror Gamer Nikson – Old dog,New tricks** (307.622 s), both original 44.1 kHz stereo FLACs. All four decode ranges and GUI interactions passed for both tracks. The 60–90 s SPUTNIK spectrum page yielded 247 detected / 408 rejected peaks, and the 120–150 s Old dog page yielded 216 / 621 with the default threshold 0.12 and 60 ms interval. SPUTNIK's final two seconds were silent and correctly yielded no peaks. These are detector outputs, not counts of manually annotated musical attacks. Timing analysis completed and exposed absolute-phase candidates on both tracks; candidate accuracy and hardware audio latency were not certified by this inspection.

### Manual measurement iteration validation

On 2026-10-04 the extended case passed for five original user-uploaded stereo FLAC files: **II-L – SPUTNIK-1, SPUTNIK-2, SPUTNIK-7, SPUTNIK-9** (each 173.647 s) and **Horror Gamer Nikson – Old dog,New tricks** (307.622 s). This includes 20 decoded ranges and five complete mouse-driven measurement/apply/undo/redo runs, with 45 actual Qt screenshots and 20 JSON outputs. Every manual End was initially chosen from an available detected transient 200–600 ms after a Start on the fixture's 120 BPM reference grid, then dragged three pixels. These points exercise the editing pipeline; they do not establish the tracks' real BPM or correct beat interpretation.

All 17 CTest targets passed across the full run and a final five-target rerun (13.43 s). The initial restored environment lacked GStreamer decoding plugins, causing the audio UI and converter cases to fail/timeout; after installing the runtime plugins, all five Analysis/core targets passed. No source workaround or submodule pin change was needed. Linux Qt 6.4.2 / GCC 13 was used; audible output and native Windows/macOS appearance were not checked.

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

Original implementation verification: the application and all test binaries built with Qt 6.4.2 / GCC 13 on Linux. All 15 CTest targets passed across the full regression run and the final focused rerun of the two Analysis targets after UI fixes. Actual offscreen screenshots were inspected for compact default width, full canvas height, timing labels, stereo separation, readable diagnostic text and minimal bottom controls.
