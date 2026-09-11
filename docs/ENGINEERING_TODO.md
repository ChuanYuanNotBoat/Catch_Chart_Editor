# Engineering TODO

Last audited: 2026-09-12

Target: Beta v1.11.1 maintenance line (`v2-main`)

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
- [ ] Replace full-Chart plugin undo snapshots with validated delta commands where possible; retain a bounded fallback for opaque mutations.
- [x] Move full-chart statistics off the UI thread with a snapshot/revision stale-result guard.
- [x] Move expensive process-plugin work off the UI thread with cancellation, timeout, and bounded-payload guards.
- [ ] Add an interval index for rain rendering, preview lookup, hit testing, and audio scheduling.
- [ ] Replace remaining whole-note scans in selection and hit testing with maintained indices.
- [ ] Cache transformed paste/mirror previews and update them only when the gesture offset, timing, or source selection changes.
- [ ] Replace working-copy/BPM/conversion polling loops and nested `processEvents()` calls with signal-driven async jobs.
- [ ] Move large resource copies, sidecar sync, and save hashing off the UI thread behind an ordered document transaction.
- [ ] Consolidate the duplicate diagnostics exporters and atomically write the remaining user-facing report/skin-configuration files.
- [ ] Add interaction benchmarks for moving 1 / 100 / 4,000 notes and resizing rain tails.
- [ ] Add load/save benchmarks at 5k / 20k / 100k notes and fail CI on major regressions.
- [ ] Profile the Windows top-level backing-store/update path behind the 10.5 ms p95 measurement.
- [ ] Replace the global ADS-leaf layout with stable Editor/Sidebar/Auxiliary Sidebar/Bottom Panel workbench parts and pane containers that cache expansion, order, visibility, and size by stable ID.
- [ ] Add explicit `Move View...` and `Reset View Location` commands before replacing raw floating docks with auxiliary pane-container windows.

## P2 — CCE-internal document architecture

The product direction is decided in [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md):
CCE remains the source of truth while these boundaries mature; repository-level
core extraction comes later.

- [ ] Introduce strong persistent IDs for document, difficulty, timeline, layer, and note entities.
- [ ] Replace floating-point beat ordering with normalized rational comparison.
- [ ] Migrate selection identity from note indices to note IDs while retaining fast render indices.
- [ ] Add a read-only versioned document snapshot and remove UI access to unrestricted mutable Chart state.
- [ ] Replace whole-Chart command snapshots with validated serializable deltas and inverse operations.
- [ ] Split `MainWindow` incrementally into session, workspace, document, command, resource, diagnostics, and plugin-UI coordinators.
- [ ] Establish a no-QtWidgets CMake target inside this repository; do not freeze or split its public ABI yet.
- [ ] Model a legacy `.mc` as one difficulty using one Base layer without changing current user-visible behavior.
- [ ] Define layer-aware clipboard commands for copy/cut/paste, atomic cross-layer and cross-difficulty transfer, and Timeline remapping previews.
- [ ] Define project-shared, inheritable/overridable, per-difficulty-required, and structural Meta field policies.
- [ ] Add transactional create/duplicate Difficulty services before exposing “New MC” in the project UI.
- [ ] Add `ComposedChartView` and revision-driven per-layer/visible-object indices; never concatenate and sort all layers per paint.
- [ ] Define the versioned directory-form project format, schema migration, atomic recovery, and resource hashing.
- [ ] Implement layer composition and deterministic export projection before exposing the full layer UI.
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
