# Engineering TODO

Last audited: 2026-09-19

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
- [x] Replace full-Chart plugin undo snapshots with validated delta commands where possible; retain a bounded fallback for opaque mutations.
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

## P2 — CCE-internal document architecture

The product direction is decided in [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md):
CCE remains the source of truth while these boundaries mature; repository-level
core extraction comes later.

- [ ] Introduce strong persistent IDs for document, difficulty, timeline, and layer entities. Do **not** require blanket persistent IDs for every Note: ordinary Note identity is semantic/content-based across saved states, while runtime handles or opt-in tracking identities may be used only when a long-lived external reference actually needs to follow a specific object.
- [x] Replace legacy Note/BPM floating-point ordering with normalized rational comparison (`BeatPosition`); preserve source triplets and use exact text-input range boundaries. Audio time caches and continuous curve geometry remain floating-point projections.
- [ ] Migrate selection away from fragile note-array indices while retaining fast render indices; use session/runtime object handles or equivalent stable-in-memory keys rather than making persistent Note IDs a prerequisite.
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

### P2 product/UX constraints — multi-document workspace

- [ ] Add multi-document chart tabs while preserving the current single-chart editing layout and interaction feel as the default baseline. Opening one chart should behave essentially like current CCE; the feature must not force a new multi-pane layout.
- [ ] Keep one primary editor view per chart. Do not introduce duplicate main views for the same chart as part of this feature. The Analysis Editor remains its separately designed window and follows the active main chart tab instead of becoming another document tab.
- [ ] Preserve explicit Replace Current behavior alongside Open in New Tab. Opening a second chart must not make every future Open action accumulate tabs by default.
- [ ] Audit current chart-open initialization behavior and classify editor state as window/global, per-document-restored, reset-on-open, or transient/cancelled. Reuse current open-new-chart behavior as the baseline rather than inventing a second set of defaults.
- [ ] Keep classic/ADS layout state independent from document workspace state. Existing splitter sizes, panel locations, visibility, and other established layout behavior must remain compatible; document switching must not silently replace the user's preferred layout.
- [ ] Preserve per-chart working state where useful (for example current time/scroll position, selection, undo/redo and dirty state), while cancelling unsafe transient interactions such as active drags, paste previews, or incomplete gestures on chart switch.
- [ ] Keep the clipboard workspace-wide so copy/paste works across open chart tabs; default paste timing semantics remain relative beat/subdivision, and SOUND notes remain excluded by existing paste behavior.
- [ ] Support temporary multi-editor presentation only on demand: Open Beside / split for comparison and Move to New Window / detachable editor windows within the same CCE instance. These are secondary presentation modes, not the default layout.
- [ ] Add file-entry UX for the multi-document model: Open/Replace Current, Open in New Tab, multi-file Open as Tabs, and drag/drop of .mc/.mcz with an explicit Replace Current vs Open in New Tab choice.
- [ ] For .mcz imports containing multiple .mc difficulties, allow selecting/importing multiple difficulties as tabs in one operation without requiring all imported difficulties to remain open.

## Later milestones

Multi-difficulty projects, reusable/shared layers, layer groups, cross-difficulty
references, review threads, Git-like checkpoints, osu!ctb, the conditional mobile
track, eventual core extraction, and optional real-time collaboration are sequenced
with explicit entry/exit gates in [FUTURE_ROADMAP.md](FUTURE_ROADMAP.md). They are
not duplicated as unchecked implementation items here until their prerequisite
milestone becomes active.

Recorded review/comment design constraints for that later milestone:

- Comments are position/range-first annotations. A comment may anchor to a time point,
  time range, time+X region, BPM/timing position, or empty space without requiring a
  Note to exist.
- Object references are optional attachments to an anchor, not the identity of the
  comment itself. A referenced Note/timing object may move with the comment while the
  reference is active; deleting or detaching the object must preserve the comment at
  a meaningful last-known anchor and mark the reference as changed/removed rather
  than silently deleting the thread.
- `CommentThread` and review-author identities require stable UUIDs. Ordinary Notes do
  not gain persistent identity merely to support comments; if a long-lived reference
  truly needs object continuity, use an explicit tracking identity only for that
  referenced object.
- Review author profiles are local-first: randomly generated stable author UUID plus
  display name, with optional exported/imported identity metadata for multi-device
  continuity. Do not derive identity from BIOS UUID, MAC address, machine GUID, or
  other hardware fingerprints.
- Review history is event-based rather than storing only the latest text. Preserve
  create/edit/reply/resolve/reopen plus anchor/reference changes as ordered events so
  imported review packages retain provenance and can show how a thread evolved.
- Event logs may use a hash chain for tamper evidence and local-history comparison.
  A hash chain proves internal log continuity, not cryptographic authorship; signatures
  or a server-backed account system are deferred until a real authentication need
  exists.
- Exported/imported review packages should carry author UUID + display-name metadata,
  thread UUIDs, and the relevant event history so comments from the same author can be
  grouped without requiring an online account.

Recorded persistent difficulty-group design constraints:

- A persistent chart/difficulty group is a relationship between difficulties of the same song, surfaced with browser-like tab grouping when members are open. Group membership is independent from open-tab state: opening one member must not automatically open all members.
- Importing multiple .mc files from one .mcz creates one group by default. Closing every member tab does not delete the group.
- .ccepr export defaults to the entire group, with an explicit option to export only the current/specified difficulty. Importing a group package restores group membership without forcing every difficulty open.
- Group-aware controls are optional and hidden when the active chart has no group. Per-group policy may override Settings defaults; Settings should define defaults for newly created/imported groups rather than silently rewriting existing group behavior.
- Group synchronization is opt-in. Title/artist and similar safe metadata plus background resources may support Off / Manual / Auto policies. Creator and Difficulty remain difficulty-local and are never group-synchronized.
- A local user profile supplies a default Creator value only when creating a new chart/difficulty. The resulting Creator field is ordinary editable chart metadata and is not dynamically bound to the profile or group.
- Offset synchronization is available only after source/target audio timelines pass compatibility validation. Initial automatic compatibility may be conservative (for example identical audio-content hashes); future explicit audio alignment may widen compatibility.
- BPM/Timing synchronization is manual-only; do not provide automatic BPM/Timing propagation. Conflicting target timing requires explicit confirmation, and synchronizing into a target that already contains notes requires a second high-risk confirmation because beat positions may map to different audio times.
- Timing-sync UX should support impact preview where practical (affected note count and representative/max audio-time shifts) so the second confirmation communicates actual consequences rather than only asking 'Are you sure?'.
- Different audio files are allowed inside one group. Audio-incompatible members still participate in group metadata/background/.ccepr workflows, but cannot use direct difficulty overlay or synchronized audio-position features.
- Audio-compatible members may later opt into preserving playback position when switching difficulty, difficulty overlays, linked navigation, compare tools, and related cross-difficulty features. None of these should become mandatory merely because charts share a group.

## Acceptance gates

- Every data-loss or out-of-root deletion path has a deterministic regression test.
- Undo/redo produces byte-equivalent logical chart content after bulk edits.
- No per-pointer-event full-chart cache rebuild during native move/rain-tail drag.
- Opening, editing, recovering, saving, and reloading the reference chart preserves its notes and resources.
- Release test suite is green, benchmark artifacts are kept out of Git, and tracked documentation has no stale internal links.
