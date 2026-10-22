# Engineering TODO

Last audited: 2026-10-23

Target: Beta v1.11.2 maintenance line (`v2-main`)

## Strategy

Use a **stabilize first, refactor with evidence** sequence:

1. Fix data-safety and correctness defects that remain valid under any future UI design.
2. Remove measured editor hot paths without changing the public chart or plugin format.
3. Introduce only small reusable seams (bulk mutations, safe paths, coalesced work).
4. Evolve the document and command boundaries inside CCE before any repository-level core split.
5. Allow mobile to consume a narrow compatibility surface only after that surface passes the desktop behavior and performance gates.

The future requirements and milestone gates are now defined in
[FUTURE_ROADMAP.md](FUTURE_ROADMAP.md). This keeps the current branch safe to
use, avoids another prematurely frozen core fork, and still creates reusable
boundaries inside the desktop repository.

## BPM advanced editing — confirmed scope (2026-10-18)

The user confirmed BPM curve display, manual measurement, BPM interpolation,
and the shared preview/apply workflow, with the main editor remaining usable.
The implementation reuses the later measurement/interpolation design described
in [ANALYSIS_EDITOR.md](ANALYSIS_EDITOR.md). Behavior and update boundaries are
documented in [BPM_ADVANCED_EDITOR.md](BPM_ADVANCED_EDITOR.md).

- [x] Add one reusable modeless window from the BPM panel, with a synchronized
      chart BPM curve, exact timing inspection and seek without autoplay.
- [x] Share existing manual measurement and interpolation tools through a small
      timing-view interface; preserve exact triplets, error bounds and one-step Undo.
- [x] Bound curve paint work; run interpolation and impact calculation on one
      low-priority worker with coalescing, revision/generation guards and no close wait.
- [x] Keep application confirmation and JSON export modeless in this window;
      invalidate pending proposals when editing continues.

The user has discarded the old BPM requirements because of design flaws. Their
original list and branch dependencies are [archived](archive/BPM_LEGACY_REQUIREMENTS.md)
for a second design, not active TODO items or prerequisites. Do not reintroduce
them from `AI_CONTEXT_MEMORY.md`. AE/AC algorithm work remains deferred.

## Reference workload

Primary stress chart: `Yugami - Nyanpasu- Lv.16`.

Baseline captured on 2026-09-09:

- 4,461 notes (4,456 normal, 4 rain, 1 sound), 1 BPM entry.
- 10-second Release playback run: canvas 59.994 FPS, preview 59.994 FPS.
- Canvas paint p95 2.648 ms; preview paint p95 2.295 ms.
- Window update p95 10.502 ms; skipped frames 0%; detected stalls 0.
- Chart parse 195 ms; session working-copy creation 61 ms.

P0 final verification on the same machine/chart:

- Chart parse was 31-35 ms (82.1%-84.1% lower than baseline); working-copy creation remained I/O-bound at 60-89 ms.
- Representative passing 10-second Release runs: canvas/preview 59.89-59.99 FPS, canvas paint p95 2.786-3.288 ms, preview paint p95 2.388-2.479 ms, window update p95 6.002-9.670 ms.
- All passing runs had 0 skipped display refreshes and 0 UI stalls. The final delivery run was 59.988 FPS / 3.191 ms canvas paint p95 / 2.416 ms preview paint p95 / 6.638 ms window update p95; working-copy creation was 89 ms and parsing was 32 ms.
- A separate final-code run that hit 17 external 33.9 ms display intervals was retained as a failed artifact; the identical immediate rerun passed. Paint, dispatch, and UI-stall budgets passed in both runs.
- Chart-loaded signal to audio-load start fell from about 537 ms to 2-4 ms after full statistics moved off the UI thread.

Do not treat FPS alone as the success criterion. Editing latency, load/save time, crash recovery, large selections, long rain overlap, and plugin-enabled input must also be checked.

## P0 — stabilize and remove deterministic hot paths

- [x] Reject recovery cleanup, manifest, load, and snapshot paths outside the session-working-copy root; add traversal/prefix regression coverage.
- [x] Use atomic replacement for `.mc`, recovery-manifest, editor-stat, existing resource/sidecar writes, and final `.mcz` publication while retaining the fast path for new working-copy files.
- [x] Persist a debounced recovery chart snapshot after edits, rather than only updating the manifest.
- [x] Keep the previous working session/recovery manifest until a replacement chart has parsed and applied successfully.
- [x] Fix the duplicated `.editor-stats.json.editor-stats.json` suffix.
- [x] Fix interval-copy clipboard mutation and cover it with a unit test.
- [x] Add bulk note mutation primitives and use them in loading, batch add/remove/move, undo, and redo.
- [x] Fix long-rain visibility lookup when rain end times are not monotonic.
- [x] Stop plugin mouse-move throttling from suppressing native editor dragging.
- [x] Remove the duplicate unsaved-changes prompt after the New Chart workflow has already confirmed replacement.
- [x] Disconnect every signal when swapping chart controllers.
- [x] Coalesce density/stat refreshes and avoid work on unrelated chart changes.
- [x] Replace in-place drag mutations with lightweight visual previews, committing only once through the undo stack.
- [x] Keep the presentation clock continuous before the first audio-position callback and cover zero-warmup startup at 0.1x-10x.
- [x] Make the primary chart workspace non-closable and add discoverable main-toolbar/menu recovery actions for every other panel.
- [x] Persist and reset classic splitter/sidebar state independently from the multi-window ADS layout.
- [x] Keep docked compact ADS tool modules content-height, release those constraints in floating containers, and route space released by closing a module to flexible editor panels.
- [x] Protect the primary editor as a non-closable/non-movable/non-floating workbench part with a tested minimum interaction surface, side-only drop targets, and dynamic docking previews.
- [x] Re-run Release tests and the `Yugami - Nyanpasu- Lv.16` benchmark; record before/after numbers.

## P1 — measured follow-up

- [x] Add monotonic chart revisions and typed change sets (`notes`, `timing`, `metadata`, `resources`).
- [x] Make render, statistics, density, selection, and reward caches revision-driven.
- [x] Replace full-Chart plugin undo snapshots with validated note commands and general `ChartDelta` records; retain the bounded opaque-input compatibility path without whole-Chart history snapshots.
- [x] Move full-chart statistics off the UI thread with a snapshot/revision stale-result guard.
- [x] Move expensive process-plugin work off the UI thread with cancellation, timeout, and bounded-payload guards.
- [x] Add an interval index for rain rendering, preview lookup, hit testing, and audio scheduling.
- [x] Replace remaining whole-note scans in selection and hit testing with maintained indices.
- [x] Cache transformed paste/mirror previews and update them only when the gesture offset, timing, or source selection changes.
- [x] Replace working-copy/BPM/conversion polling loops and nested `processEvents()` calls with signal-driven async jobs.
- [x] Move large resource copies, sidecar sync, and save hashing off the UI thread behind an ordered document transaction.
- [x] Consolidate the duplicate diagnostics exporters and atomically write the remaining user-facing report/skin-configuration files.
- [x] Add interaction benchmarks for moving 1 / 100 / 4,000 notes and resizing rain tails.
- [x] Add load/save benchmarks at 5k / 20k / 100k notes and fail CI on major regressions.
- [x] Profile the Windows top-level backing-store/update path behind the 10.5 ms p95 measurement.
- [x] Replace the global ADS-leaf layout with stable Editor/Sidebar/Auxiliary Sidebar/Bottom Panel workbench parts and pane containers that cache expansion, order, visibility, and size by stable ID.
- [x] Add explicit `Move View...` and `Reset View Location` commands before replacing raw floating docks with auxiliary pane-container windows.

## P1 follow-up — unified keyboard shortcuts (2026-10-01)

- [x] Inventory commands, default bindings, scopes and intentionally reserved input
      in [KEYBOARD_COMMANDS.md](KEYBOARD_COMMANDS.md).
- [x] Share stable IDs and `CommandRouter` dispatch across menus, canvas and floating
      panels; remove default-key bypasses and the F8 manual stutter marker.
- [x] Drive settings and runtime reference from the registry; preserve disable/reset,
      reject exact/prefix collisions in overlapping scopes and allow exclusive modes.
- [x] Preserve text-input, dialog and popup keys; keep curve/process-tool behavior
      and dispatch editing commands from real ADS floating panels.
- [x] Verify with actual MainWindow/ChartCanvas GUI regression tests: remap/disable/
      reload/reset, multi-stroke commands, conflict edits retained, native curve and
      process-tool dispatch, selection without seeking, and mode changes without scroll.

AutoTiming algorithms continue in the independent AutoTimingCore repository;
CCE work here is limited to integration and editor behavior.

## AE / AC follow-up — completed host work (2026-10-08)

Detailed scope, current-source audit, historical issue mapping and implementation
order are in [the archived AE / AC plan](archive/AE_AC_GUI_BUGFIX_PLAN_2026-10-08.md).
Normal / Advanced / Debug and the host fixes below are implemented on
`feat/ae-analysis-workbench`. Debug/Release native builds and the same 28 focused
host cases passed in both configurations (30 QtTest passes including setup/cleanup).
The stopped AutoTiming accuracy/audio/smoke runs remain stopped.

- [x] Fix AE result/config identity, range clamping and source replacement guards.
- [x] Unify musical-time/audio-time transforms; preserve exact Malody beat triplets
      and distinguish chart timing, measured audio positions and proposal models.
- [x] Extract a small analysis session/task model and shared Event/Curve/Region/
      Candidate tracks; keep existing Spectrum/Transient/Core algorithms.
- [x] Add stable Advanced config/Profile compatibility and version-bound Debug controls:
      read-only Default, Save As/copy, section/full reset, JSON import/export,
      global preset/project override and effective snapshot/hash.
- [x] Complete the three interface levels, musical/absolute views, whole-audio
      energy overview and compact Spectrum/right Note lane layout.
- [x] Share timing proposal/preview/validation and AE-scoped editing commands;
      add continuous/segmented tempo-map preview while retaining candidate grids.
- [x] Expand bridge diagnostics with SignalMetrics, EvidenceReason and raw rhythm
      candidates; add Overview/Compare, Tempo/Phase, Rhythm/Evidence and
      Reference/Tests views using existing local records.
- [x] Verify affected AE/config/bridge behavior during implementation; keep AC
      accuracy fixes, true Core cancellation and gitlink updates as separate work.

The iteration also corrected duplicate offset handling in BPM-map projection.
See [the implementation record](archive/AE_AC_GUI_BUGFIX_PLAN_2026-10-08.md#10-实施记录2026-10-08)
and [current AE usage](ANALYSIS_EDITOR.md). Full `.ccepr`, random-access PCM cache,
universal CCE TaskContext infrastructure and the independent Core candidate remain
separate follow-ups; this completion does not claim they are implemented.

## AE spectrum follow-up (2026-10-18)

- [x] Share immutable stereo 2-second spectrum blocks and in-flight requests between AE and the optional Catch canvas overlay; assemble analysis pages without repeating FFT for overlapping viewports.
- [x] Prioritize visible blocks, display completed blocks progressively, prefetch one adjacent block and retain visible data during asynchronous updates.
- [x] Keep 5ms / 192-band detail with bounded memory/disk caches and an optimized standalone Debug DSP backend.
- [x] Add a preference-backed main spectrum overlay beneath notes/grid, preserve typed timing/resource updates and avoid a second playback clock or waveform.
- [x] Add isolated, muted playback benchmark options and verify the spectrum/AE integration with focused tests; see [SPECTRUM_PIPELINE.md](SPECTRUM_PIPELINE.md).

Random-access PCM caching, true Core cancellation, full project packaging and
the document/layer roadmap below remain open; a spectrum-tile cache does not
complete those requirements.

## AE feature follow-up

- [x] Replace the Generation placeholder with a detected-transient draft workflow: range/band/strength/snap filters, per-note review, passive ghosts in both editors and one explicit undoable commit.
- [x] Verify draft source/revision guards and shared cancellation through the configured command; preserve read-only inspection and existing measurement gestures. Debug build and six focused cases passed on 2026-10-18 (8 QtTest passes including setup/cleanup).
- [ ] Evolve charting rules and playable X patterns only through separate reviewed draft-generation features; do not infer them from onset width or diagnostic tempo models.

## P2 — CCE-internal document architecture

The confirmed local review/layer iteration is specified in
[LOCAL_REVIEW_IMPLEMENTATION.md](LOCAL_REVIEW_IMPLEMENTATION.md). It supersedes
the persistent Note-ID prerequisite below: Note identities remain runtime-only;
review changes use data matching and explicit editing intent. Project, layer,
difficulty, comment, author and revision IDs remain persistent.

- [x] P1: editable document snapshots, canonical hashes and structured Diff.
- [x] P2: shared layers, independent bindings, live references and export.
- [x] P3: local signing identities and independent persistent comments.
- [x] P4: PR Base/Proposal views, explicit Move intent and Note Inspector.
- [x] P5: versioned `.ccepr` roundtrip and revision-DAG merge.
- [x] P6: explicit dependency groups, three-way merge/rebase, transactional failure rollback and one-step Undo/Redo; dependency groups are rebuilt after branch merges.
- [x] P7: group-move bounds, unresolved-comment filtering, direct proposal tools, narrow-window layouts and a tool-page-only top bar; layers and PR are visibly experimental and optional.
- [x] P8: requirement-by-requirement acceptance, stale results and explicit history events; Debug/Release CTest, native GUI and isolated real-chart roundtrips passed. Historical diagnostic limits are recorded in [TESTING.md](../TESTING.md).

The product direction is decided in [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md):
CCE remains the source of truth while these boundaries mature; repository-level
core extraction comes later.

- [ ] Introduce strong persistent IDs for document, difficulty, timeline and layer entities; match Notes from data without persistent Note IDs.
- [x] Replace legacy Note/BPM floating-point ordering with normalized rational comparison (`BeatPosition`); preserve source triplets and use exact text-input range boundaries. Audio time caches and continuous curve geometry remain floating-point projections.
- [x] Use runtime Note IDs for selection identity while retaining index-based render access; Note IDs remain runtime-only and are not serialized.
- [x] Add a read-only versioned `ChartSnapshot`/`ChartReadVersion`, remove `mutableChart`/canvas mutable access, and use source/domain revision guards for asynchronous statistics. Captures share Note/BPM storage; loading another chart invalidates old tokens. Project-wide read models and composition indices remain separate below.
- [x] Replace whole-Chart command snapshots with validated serializable `ChartDelta` records and inverse operations for document/opaque-plugin/legacy batch commands. Preserve raw data, ordering and runtime handles in session; serialized replay binds by source data/position without Note IDs. See [DOCUMENT_TRANSACTIONS.md](DOCUMENT_TRANSACTIONS.md).
- [ ] Replace auxiliary `ProjectBundle` history snapshots with project-domain transactions; unify native commands into a serializable journal with persistent base/revision envelopes and recovery replay, then measure complete Workspace long-history memory and interaction costs.
- [ ] Split `MainWindow` incrementally into session, workspace, document, command, resource, diagnostics, and plugin-UI coordinators.
- [x] Establish a no-QtWidgets QtCore CMake target (`CceReviewCore`) inside this repository; do not freeze or split its public ABI yet.
- [x] Model an imported legacy `.mc` as one difficulty using one Base layer; document-model tests cover the composition and roundtrip behavior.
- [ ] Define layer-aware clipboard commands for copy/cut/paste, atomic cross-layer and cross-difficulty transfer, and Timeline remapping previews.
- [ ] Define project-shared, inheritable/overridable, per-difficulty-required, and structural Meta field policies.
- [ ] Add transactional create/duplicate Difficulty services before exposing “New MC” in the project UI.
- [ ] Add `ComposedChartView` and revision-driven per-layer/visible-object indices; never concatenate and sort all layers per paint.
- [ ] Define the versioned directory-form project format, schema migration, atomic recovery, and resource hashing.
- [x] Implement layer composition and deterministic `.mc` export projection; document-model and export-pipeline tests cover visibility/export eligibility and selected-layer projection. Revision-driven composition indices remain tracked separately above.
- [ ] Keep QWidget/QPainter unless measured multi-layer workloads demonstrate a rendering-backend limitation.
- [ ] Redesign the process-plugin protocol around async requests, cancellation, capability scopes, bounded payloads, and layer-aware deltas.

## Later milestones

Multi-difficulty projects, reusable/shared layers, layer groups, cross-difficulty
references, review threads, Git-like checkpoints, osu!ctb, the conditional mobile
track, eventual core extraction, and optional real-time collaboration are sequenced
with explicit entry/exit gates in [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md). They are
not duplicated as unchecked implementation items here until their prerequisite
milestone becomes active.

## Acceptance gates

- Every data-loss or out-of-root deletion path has a deterministic regression test.
- Undo/redo produces byte-equivalent logical chart content after bulk edits.
- No per-pointer-event full-chart cache rebuild during native move/rain-tail drag.
- Opening, editing, recovering, saving, and reloading the reference chart preserves its notes and resources.
- Release test suite is green, benchmark artifacts are kept out of Git, and tracked documentation has no stale internal links.
