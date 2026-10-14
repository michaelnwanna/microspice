# Phase 5: Desktop Projects and Editing

The optional Qt Widgets application lives in `gui/`. It owns schematic
geometry, connectivity extraction, project persistence, and edit history.
The existing simulation core still contains no Qt dependencies.

## Project Files

Open and Save use the `.microspice` extension. The contents are readable JSON
with `format: "microspice.project"` and `version: 1`. The checked-in
[voltage divider](../examples/voltage_divider.microspice) is a complete example.

Each component stores:

- `kind`: 0 resistor, 1 capacitor, 2 diode, 3 voltage source, 4 current source,
  or 5 ground.
- `reference`: a unique device reference such as `R1` or `G1`.
- `position`: the symbol center as `[x, y]`, in schematic units.
- `rotation`: 0, 90, 180, or 270 degrees around that center.
- `value`: resistance in ohms, capacitance in farads, diode saturation current
  in amperes, source voltage in volts, source current in amperes, or zero for ground.
- `ideality`: the diode's dimensionless ideality factor; retained for every
  component to keep a uniform record.

Each wire stores `start` and `end` coordinate pairs. `counters` stores six
reference counters in component-kind order so deleting, saving, and reopening
does not normally reuse references. Loading also accounts for the highest
reference actually present. Selection, zoom, analysis results, and undo history
are not part of the project format.

`ProjectState` is the document snapshot. `serializeProject` and `parseProject`
use Qt's structured JSON APIs. `readProject` reports read/parse errors;
`writeProject` uses `QSaveFile` so a failed save does not truncate an existing
project. The document's filename and clean state change only after a successful
save or open. Loading invalid data leaves the current scene intact.

## Validation

Projects may be incomplete: an unfinished circuit can be saved without ground
or complete wiring. Electrical validity is checked when analysis runs.

File-level validation rejects unsupported versions, missing required fields,
wrong JSON types, nonfinite values, duplicate references, unsupported component
kinds, invalid rotations, nonpositive passive/diode values, and diagonal or
zero-length wires. Source values may be signed or zero.

Coordinates must lie on the 20-unit grid within +/-1,000,000 schematic units.
Limits are 16 MiB per input file, 10,000 components, and 50,000 wire segments.
These are input validation limits, not performance targets: the editor and
dense simulation core are intended for small educational circuits.

## Undo and Document State

`QUndoStack` owns edit commands and exposes the native undo/redo actions.
Commands retain before/after `ProjectState` snapshots instead of pointers to
graphics items. Undo and redo reconstruct the scene and refresh selection-based
properties. Snapshot history is appropriate for the current small schematics;
larger editors could replace it with object-level commands later.

Placement, wire creation, component values, diode ideality, movement, rotation,
and deletion are undoable. A drag captures its original state and commits once
on mouse release. A two-segment wire also commits as one command. Selection and
cancelled previews do not create edits. The undo limit is 100 commands.

Saving marks the stack clean. Undoing back to the save point clears the modified
indicator; making a new edit marks it dirty. New and Open reset history.
New, Open, and Close offer Save, Discard, or Cancel for modified projects;
cancelling a save or encountering a save error aborts the replacement/close.

## Connected Movement

After a move or rotation, attached endpoints reroute with orthogonal bends.
Shared terminal locations and wire-interior junctions remain fixed and get a
lead to the moved pin. Selected groups use the original gesture geometry as
their routing baseline. Replacing wires occurs after mouse release so Qt can
finish processing selections that include wire items. Geometry and routing
belong to the same undo command.

This is simple routing, not obstacle-aware routing. A new bend may touch other
wires, and the editor's normal endpoint-junction rule still applies. Inspect
such layouts and undo or redraw as needed. Interior-only crossings remain
unconnected; explicit endpoint touches and terminals on wires connect.

## Verification and Remaining Work

The waveform dock uses the same `solveTransient` and `solveDcSweep` functions
as the CLI. Transient controls expose timestep, stop time, and capacitor initial
voltages. Sweep controls choose a voltage source and a signed source range.
Waveform adapters retain plot-ready voltage/current traces, the core CSV
output, and bounded per-step solver diagnostics. Each selectable trace
has a labelled independent vertical scale. Horizontal zoom, pan, reset, and a
nearest-sample cursor support inspection. CSV saves use `QSaveFile`.

Analysis settings and results are session-local, not part of version-1 project
JSON. Circuit edits and setting edits invalidate results and export. Different
projects reset capacitor initial values. Transients containing diodes are
rejected by the core; linear capacitors are open circuits in DC sweeps.

Interactive waveform analysis is capped at 2000
samples and a diagnostic budget of 4,000,000 scalar slots, estimated from sample
count and MNA size (with a conservative factor for nonlinear sweeps). These
bounds do not promise latency for every circuit; use the CLI for larger runs.

## Background Execution

`BackgroundJob<Result>` runs a captured work function with `std::async` and polls
its future using a UI-thread `QTimer`. Work captures value snapshots, never Qt
widgets or the live scene. Completion and error callbacks run on the UI thread.
Atomic cancellation/progress state is the only mutable data shared with workers.
DC and waveform Run controls coordinate busy state to prevent overlapping runs.
Waveforms display sample/point progress; both paths expose stop controls.

The Qt-free core accepts optional `ExecutionControl` callbacks without changing
default synchronous behavior. Cancellation throws `AnalysisCancelled` at matrix
pivots, Newton iterations, timesteps, and sweep points. Circuit and setting edits
cancel affected work and mark its completion for discard, even if the worker has
already finished. User cancellation also suppresses completed-but-unpublished data.
Closing cancels and waits for workers before destroying their captured state.
Cancellation latency depends on reaching a checkpoint; extraction, stamp assembly,
CSV conversion, and UI publication are not interruptible within each operation.
Circuit extraction and final UI publication remain on the UI thread.

The synchronous panel/DC helpers remain available to deterministic tests; user
Run controls use background workers. Existing diagnostic budgets are retained to
bound memory, not to guarantee latency or enable arbitrarily large circuits.

## Probes and Solver Diagnostics

The Probe tool resolves terminals and connected wire segments to the extracted
node labels. Clicking a voltage-source body selects its solved branch current.
The waveform panel selects the matching trace, including an explicit zero-volt
ground trace. DC operating-point readings are annotated on the schematic.
Probes are read-only and do not affect history or project state. Interior-only
crossings remain separate; an exact crossing click chooses one wire, so click
away from the crossing when choosing a specific net.

`DiagnosticFrame` retains a labelled MNA matrix, RHS, solution, stamps, capacitor
history, and Newton iterations as applicable. `SolverInspector` selects a DC
frame, transient timestep, or sweep voltage and displays these in read-only tabs.
Newton row selection shows each diode's tangent model and solved voltage.
The core retains only the final Newton matrix, not a matrix per iteration.
Transient stamp operations describe static devices; companion conductance and
history current appear in the capacitor table instead.

Nonconverged DC attempts retain their iterations and explicitly report failure
in the inspector. Failed sweeps currently report an error without partial results.
Circuit edits clear all diagnostics and probe readings. Waveform-setting changes
invalidate waveform diagnostics but leave a newer DC inspector result intact.
Analysis settings, diagnostics, probes, and readings are not saved in project JSON.

Offscreen tests cover project roundtrips, sample and reopened divider solves,
schema rejection, failed I/O, Save/Discard/Cancel decisions, close guards,
undo/redo, history bounds, connected moves and rotations, and real mouse
gesture grouping. A full-window smoke test exercises properties, linear and
nonlinear DC, dirty indicators, undo actions, save/reopen, and captures large
and compact window images. Waveform tests verify saved RC charging against
backward Euler, capacitor initial values, divider and diode sweeps, CSV output,
negative steps, size/error reporting, trace rendering/selection, zoom/pan,
single-point ranges, and result invalidation. Full-window tests also exercise
the Analysis menu, transient/sweep execution, and CSV export.
Probe tests include mouse interaction without history changes, disconnected
crossings, endpoint junctions, ground, voltage/current selection, and stale-reading
invalidation. Inspector tests compare labels and values against core results,
select transient and sweep frames, exercise Newton/diode row selection and failed
convergence, and capture desktop and compact views. Diagnostic adapters verify
matrix-times-solution agrees with the retained RHS.

Background tests cover worker/UI thread ownership, result and progress delivery,
cancellation after completion, exceptions, reuse, and teardown. Panel/window tests
cover asynchronous runs, stop controls, nonoverlap, worker errors, event-loop
responsiveness, and circuit/settings invalidation while a result is pending.

## macOS Bundle

The Apple GUI target is a versioned `MACOSX_BUNDLE` with a generated bitmap icon.
The Desktop install component stages the app and sample projects, deploys Qt using
`macdeployqt` with an explicit SDK framework search path, and ad-hoc signs the final
bundle. Logged deployment errors are fatal even if the tool returns success.
The installed executable uses `@executable_path/../Frameworks` rather than a local
Qt SDK runtime path. CPack generates an architecture-labelled ZIP.

`gui/verify_macos.cmake` checks deep signatures, audits framework/plugin dependency
and runtime paths, and runs the native full-window smoke test. Verification of a
relocated app exercises portability within this Mac; it does not replace testing
on other macOS versions or architectures. Developer ID signing, notarization,
redistribution-license review and other platform packages remain release work,
along with more advanced routing.

## Cross-Platform CI

The GitHub Actions workflow retains Qt-free core jobs on Linux GCC/Clang and adds
macOS Apple Clang and Windows MSVC. Its Qt 6.10.0 GUI matrix targets Ubuntu 24.04,
macOS 14, and Windows Server 2022 using Ninja and an explicit MSVC environment.
Desktop tests have per-test timeouts, and logs/screenshots upload even on failure.
The macOS job additionally stages/deploys the bundle, audits it with the native
smoke test, creates a ZIP, and uploads it as a test artifact. Artifact retention
is 14 days; these are not signed/notarized public releases.

Workflow YAML and Actions expressions can be checked locally with actionlint;
real Linux/Windows/macOS runner results require a hosted workflow run. Qt-free
jobs explicitly disable the GUI so Qt does not become a core dependency.
clang-format 18 is pinned for consistent checks of core and GUI sources.