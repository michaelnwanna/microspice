# microspice User Guide

microspice is an educational circuit simulator with an editable schematic,
DC operating points, linear capacitor transients, source sweeps, and inspectable
solver equations. This guide describes the desktop app's supported workflows.

## Getting started

1. Open **File > Examples > Voltage divider**.
2. Choose **Analysis > Run DC**, the project toolbar's play icon, or **F5**.
3. The DC operating point panel shows a 5 V midpoint for two equal 1 K-ohm
   resistors driven by 10 V. Source currents and solver iteration counts appear
   below the terminal voltages.
4. Select R1, change its value to `2K`, and commit with Enter or by leaving the
   field. Run DC again to see the changed output.
5. Use **File > Save** to save your own `.microspice` project.

The app initially opens a wired divider. Analyses are never run implicitly when
opening a project or example. Hover over toolbar icons to see their names.

## Workspace and panels

The upper toolbar holds project commands, undo/redo, Run DC and its stop action.
The schematic toolbar holds Select, Probe, component tools, Wire, Rotate, Delete,
zoom and Fit schematic. The central canvas is the editable circuit.

Properties and DC operating point are right-hand panels. Waveforms and Solver
inspector open below the canvas. **View** toggles panel visibility. Panels can
be moved, floated and closed. **View > Reset panel layout** restores the default
arrangement. Normal sessions remember window geometry and panel layout.

## Starter examples

**File > Examples** contains five offline, pre-wired circuits:

- **Ohm's law - resistor load:** 5 V across 1 K-ohm gives 5 mA.
- **Voltage divider:** equal resistors divide 10 V to a 5 V midpoint.
- **Current-driven resistor:** 2 mA through 1 K-ohm produces 2 V.
- **RC charging:** 1 K-ohm and 1 microfarad give a 1 ms time constant. The waveform
  panel opens with a `100u` timestep and `5m` stop time; press its play icon.
- **Diode bias:** a 5 V source and series resistor bias a diode at roughly
  0.57 V. Run DC and inspect Newton convergence; try a V1 sweep from 0 to 5 V.

Examples open as editable unsaved copies. Save asks for your own filename and
does not overwrite the bundled source. Replacing unsaved work still asks for
Save, Discard or Cancel.

## Placing and selecting components

Choose Resistor, Capacitor, Diode, Voltage source, Current source or Ground,
then click empty canvas to place it. Positions snap to a 20-unit grid. A tool
remains active until you choose another tool. Clicking an existing component
while a placement tool is active selects/drags it instead of adding a duplicate.

Choose Select to select items or drag a selection rectangle. Drag components to
move them. Rotate turns selected components by 90 degrees; Delete removes selected
components or wires. Edit controls are disabled when no suitable selection exists.
One completed drag is one undo step. Probe mode reads results without editing.

## Wires and connectivity

Choose Wire, click the start, then click the end. The preview uses a horizontal
segment followed by a vertical segment. Escape or right-click cancels unfinished
wiring. Zero-length segments are omitted.

Wire endpoints touching another wire create connections. Terminals touching a
wire connect to it. Interior-only crossings do not connect; add an endpoint at
the crossing when a junction is intended. Junction dots mark branches. All ground
symbols share reference node 0; a solvable circuit needs a ground reference.

Attached wire endpoints reroute when a component move or rotation finishes.
Interior/shared junctions stay anchored and get a connecting lead. Routing is
orthogonal, not obstacle-aware: inspect bends for unintended endpoint contacts.
Undo restores both movement and wiring. Check topology before interpreting results.

## Properties and engineering values

Select exactly one component to edit its properties. Resistance uses ohms
(the UI displays the omega symbol), capacitance uses F, voltage V, and current A.
Diodes expose saturation current in A and a dimensionless ideality factor.
Ground has no editable numeric value.

Inputs accept plain numbers, scientific notation and engineering suffixes:

| Input | Meaning |
| --- | --- |
| `2.2k` or `2.2K` | 2200 |
| `1M` or `1meg` | 1000000 |
| `1m` | 0.001 |
| `4.7u` | 0.0000047 |
| `1n` | 0.000000001 |
| `1p` | 0.000000000001 |

Use numeric values/suffixes, not unit text, in value fields. Resistors, capacitors
and diode saturation currents must be positive and finite. Voltage/current
sources may be positive, zero or negative. Diode ideality is positive; the editor
allows 0.01 to 100. Invalid component values stay in the field with an inline
error; the circuit keeps its previous valid value until you correct the entry.

Uppercase **M means mega in GUI input**, while lowercase **m means milli**.
The separate SPICE netlist/CLI parser follows SPICE rules: either m or M means
milli; use meg for mega. Labels automatically use readable engineering prefixes.

## Saving and undo

File provides New schematic, Open project, Save project and Save As. Files use
the `.microspice` extension and contain versioned JSON. Open accepts these project
files, not SPICE text netlists; use the CLI for `.cir` netlists.

Save records component references, positions, rotations, values, diode parameters,
wires and reference counters. Saves are atomic and loaded files are validated
before the current scene is replaced. Errors leave the current project intact.
New, Open, Examples and closing the window protect unsaved changes with
Save/Discard/Cancel. A cancelled or failed save does not discard your circuit.

Edit > Undo/Redo covers placement, properties, movement, rotation, deletion and
wiring. History retains up to 100 edits; an entire wire gesture or drag is one
step. Opening/new projects clears history. Waveforms, probe readings, diagnostics,
analysis settings and capacitor initial conditions are session-local, not saved
in project files. Opening a different project resets capacitor initial voltages.

## Canvas navigation

Scroll to zoom around the pointer. Hold the middle mouse button and drag to pan.
Toolbar/View zoom actions use the viewport center. Zoom is bounded from 0.25x
to 4x. **Fit schematic** recenters and scales down the complete circuit with
padding. In fitted mode, resizing keeps it visible; manual zoom or pan ends
automatic fitting until Fit schematic is chosen again.

## DC operating point

Run DC using Analysis > Run DC, the upper play icon, or F5. Resistors, independent
voltage/current sources and diodes are supported. Capacitors are open circuits
in DC. Voltages are measured relative to ground. Voltage-source branch currents
use the solver's source-terminal direction; a supplying source can show a
negative current. Resistor current can be calculated from its terminal voltage
difference divided by resistance.

The results panel lists terminal voltages, voltage-source currents, MNA unknown
count and Newton iteration count. Linear circuits require no Newton iteration.
Diodes use a Shockley model and Newton linearization. A nonconverged attempt
reports failure but leaves its diagnostics inspectable.

## Transient analysis

Choose Analysis > Transient or the RC charging example. Set timestep and stop
time in seconds, using values such as `100u` and `5m`. Enter each capacitor's
initial voltage in the table; defaults are zero. Press the waveform panel's
play icon, not F5, to run the selected waveform analysis.

Transient is currently linear-only: resistor/capacitor/independent-source
circuits work, but diode transients are rejected. Sources are constant-valued;
there is no sine/pulse source editor. Nonzero initial conditions provide a
starting capacitor state, not a saved waveform.

Backward Euler samples begin at the first timestep, not time zero. The final
sample is the greatest timestep multiple not beyond the stop time. A smaller
timestep gives finer resolution but more samples; runs are capped at 2000 samples
and a diagnostic-memory budget. RC charging approaches 10 V with a 1 ms time
constant for the bundled 1 K-ohm / 1 microfarad circuit.

## DC sweeps

Choose Analysis > DC Sweep. Select an ideal voltage source, then enter start,
stop and step in volts. The waveform play icon solves each point. Use a negative
step for descending sweeps; zero or incorrectly directed steps are invalid.
Diodes are supported by the nonlinear DC solver. Sweep points beyond the
endpoint are not included. The sample/diagnostic limits also apply to sweeps.

A failed sweep reports an error instead of retaining partial results or
diagnostics. Only the selected source changes; the other source values stay fixed.

## Waveforms and CSV export

The trace menu selects a node voltage or voltage-source current. Terminal
references identify voltage traces; each selected trace has its own vertical
scale and units. Scroll over the plot to zoom horizontally, drag to pan, hover
for the nearest sample readout, and use the fit/reset icon to restore its range.

The waveform save icon exports CSV to a chosen path using an atomic write.
Transient CSV contains time and node voltages. Sweep CSV contains source values,
node voltages, source currents and convergence information. The export button
is disabled without valid results. CSV may contain more data than the visible
selected trace. Export preserves the core solver's values, not pixel positions.

## Probes

Choose Probe and click a terminal or wire to select its voltage trace. Click
a voltage-source body to select its branch current. Ground supplies a zero-volt
trace. After DC, probes show operating-point readings directly on the canvas.
If a matching waveform exists, the waveform panel selects it.

At an interior-only crossing, a probe chooses one wire without connecting the
nets. Click away from the crossing to select the intended wire explicitly.
Probing changes neither the document nor undo history. Component-body currents
for resistors, capacitors and diodes are not separate probe traces.

## Solver inspector

Open Analysis > Solver Inspector or its View panel toggle. The frame selector
chooses a DC frame, transient sample or sweep point from the latest analysis.

- MNA system: labelled matrix, right-hand side and solution.
- Stamps: individual component assembly operations.
- Capacitors: previous voltage, companion conductance/history current and solved voltage.
- Newton: iteration deltas and junction mismatch; select an iteration for each
  diode's tangent voltage, conductance, current and equivalent current.

The displayed Newton matrix is the final linearized system, not a retained
matrix for every iteration. Static-device stamps and capacitor companion values
are shown separately. Diagnostics are read-only and session-local.

## Running, stopping and stale results

Analyses run on background workers using a circuit/settings snapshot. Only one
analysis runs at a time. Use the upper stop icon for DC and the waveform stop
icon for transient/sweep. Cancellation is cooperative at matrix pivots, Newton
iterations and sample/point boundaries; it may not be instantaneous.

Editing a circuit cancels pending work and clears stale DC, probe, waveform and
diagnostic results. Editing waveform settings clears the corresponding waveform
results. Select/Probe alone do not invalidate results. Closing cancels and waits
for active work; assembly or final UI operations can briefly delay cancellation.

## Script Workspace

Open **Tools > Script Workspace** to write and run `.msim` simulation programs.
This independent editor does not replace your schematic. Its example menu
contains six offline script starters; examples open as unsaved copies.

Compile / Validate checks source without solving. Diagnostics show line and
column and can be clicked to navigate to highlighted source. Run executes all
or a selected named analysis; Cancel stops pending work. Results include typed
observations, measurements, pass/fail checks, waveforms and solver diagnostics.
The export icon writes CSV for the selected successful analysis.

Save/Save As store UTF-8 source using atomic writes. New/Open/example loading,
Escape and closing protect unsaved edits; application close also checks the
script document. Editing source cancels work and clears stale results. In this
workspace F5 runs the script, and standard file, undo/redo and Find shortcuts
apply to the script instead of the schematic.

Use the workspace's Simulation language guide action for its complete bundled
syntax and usage reference. Code-to-schematic generation is not supported.

## Keyboard reference

Standard shortcuts use Command on macOS and Ctrl on Windows/Linux:

| Action | Shortcut |
| --- | --- |
| New | Command/Ctrl+N |
| Open | Command/Ctrl+O |
| Save | Command/Ctrl+S |
| Save As | Command/Ctrl+Shift+S |
| Undo | Command/Ctrl+Z |
| Redo | Platform-standard redo (Edit menu shows the assigned shortcut) |
| Zoom | Platform-standard Zoom In/Out (View menu shows the assigned shortcuts) |
| Run DC | F5 |
| User Guide | F1 |
| Delete selection | Delete |
| Cancel unfinished wire | Escape or right-click |

Use Enter or focus change to commit component values. In this guide, Enter
finds the next match; Previous/Next wrap around the document. Closing the guide
returns to the circuit without discarding work.

## Troubleshooting

- Missing ground: add a ground symbol touching the intended reference net.
- Floating/singular circuit: check disconnected pins, interior-only crossings,
  isolated sections and conflicting ideal-source constraints.
- Unexpected voltages: inspect wire endpoints, polarity and shared ground. DC
  capacitors are open, not short circuits.
- Diode nonconvergence: inspect Newton diagnostics and check values/polarity;
  no convergence guarantee exists for arbitrary circuits.
- Empty waveform: check selected trace and input values, then run the waveform
  analysis; F5 only runs DC.
- Export disabled: rerun after edits; invalidated or failed results cannot export.
- Invalid field: use finite numeric input and supported suffixes; keep unit text
  out of the input and respect positive-only component constraints.
- Hidden/misplaced panel: use View or Reset panel layout.
- Too many samples: increase timestep or sweep step, shorten the range, or use
  the CLI for larger analyses.

## Scope and limitations

The current device set is resistors, capacitors, Shockley diodes, ideal independent
voltage/current sources and ground. There are no inductors, transistors, op-amps,
555 models, controlled sources, AC analysis or diode transients. Wire routing
is not obstacle-aware. Projects do not persist analysis settings or results.

This is an educational simulator, not a certified design/production verification
tool. Cross-check important designs with established tools and hardware. Desktop
packages are unsigned/ad-hoc signed test builds; publisher signing, clean-machine
testing and licensing release gates remain separate from simulation correctness.