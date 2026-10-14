# Microspice Script

Microspice Script is a small declarative circuit language. Files use `.msim`.
It builds the same circuit model as the schematic editor, without Qt or an
intermediate SPICE-text conversion. It is available through the CLI and the
desktop Script Workspace. Existing `.cir` netlists and `.microspice` JSON
projects keep their own syntax and tools.

## Desktop Script Workspace

Choose **Tools > Script Workspace**. This separate, modeless editor lets you
keep a visual schematic and a script open without replacing either document.
There is no automatic code-to-schematic layout or bidirectional conversion.

1. Open a bundled script from the examples icon, or use Open for a `.msim` file.
    Examples are editable unsaved copies and work offline.
2. Edit the highlighted source. Use the search field or the platform Find
    shortcut, and the arrow buttons to move between matches.
3. Choose **Compile / Validate** to check syntax, units, references and limits
    without running analyses. Click a diagnostic to jump to its source location;
    errors are also underlined in the editor.
4. Select **All analyses** or a named analysis, then choose Run (F5 inside this
    workspace). Use Cancel to stop pending compilation or execution.
5. Review observations, measurements and PASS/FAIL/SKIP reports in the results.
    Select an analysis and trace for plotting. DC observations are scalar values;
    transient and sweep observations use the waveform plot.
6. Inspect retained native solver frames in the Solver inspector tab. Export
    the selected successful analysis with the CSV save icon.
7. Save your script using Save or Save As. Script files contain source text,
    not computed results. Save, undo/redo and other document shortcuts apply
    within this workspace independently of the schematic.

New, Open, example loading, Escape, and closing protect unsaved source with
Save/Discard/Cancel. Closing the main app also checks unsaved script work.
Failed reads, invalid UTF-8, oversized sources and failed saves leave the
current document intact. Writes are atomic. Source size is limited to 1 MiB.

Compilation and simulation run in the background. Editing the source cancels
pending work and clears old diagnostics/results; completed results from an
older revision are never published. Selecting an analysis does not implicitly
rerun it. The schematic's results are not changed by script execution.

The language guide is available from the workspace toolbar, bundled offline.

## First Circuit

```text
param R = 1kohm
voltage_source("V1", supply, ground, 10V)
resistor("R1", supply, output, R)
resistor("R2", output, ground, R)
dc("bias")
observe("bias", voltage(output), current("V1"))
measure("midpoint", from="bias", value=voltage(output))
check("divider output", abs(midpoint - 5V) <= 1mV)
```

Matching node identifiers connect components. `ground` is predefined. This
circuit produces 5 V at output; V1 current is negative because it supplies power.
Parameters are immutable and must be defined before use. Analyses operate on
independent copies of the base circuit, not on state left by the previous run.

```bash
cmake -S . -B build
cmake --build build
./build/microspice_script check examples/scripts/voltage_divider.msim
./build/microspice_script run examples/scripts/voltage_divider.msim
```

`check` compiles and validates without solving or evaluating runtime checks.
`run` solves the requested analyses and evaluates measurements and checks.

## Syntax and Units

One statement per line, with multiline calls allowed inside parentheses. `#`
starts a comment. Strings use double quotes. Braces delimit subcircuit bodies;
semicolons are not used. Names are case-sensitive. Component and instance names
must be nonempty and cannot contain the hierarchy delimiter `.`.

Numbers may use scientific notation. Supported base units are `V`, `A`, `s`,
`ohm`, and `F`. Prefixes are case-sensitive: `p`, `n`, `u`, `m`, `k`, `M`, `G`,
`T`. Examples: `10V`, `1kohm`, `1Mohm`, `4.7uF`, `100us`, `5ms`, `1e-12A`.
A space between number and unit is accepted. Use ASCII `u` and `ohm` in source.

Bare numbers are dimensionless: `resistor("R1", a, ground, 1000)` is rejected;
write `1000ohm`. Do not use SPICE suffixes alone such as `1k` or `1meg`.
Uppercase `M` means mega here; the unchanged SPICE parser treats `M` as milli.

Arithmetic supports `+`, `-`, `*`, `/`, unary signs, parentheses, and `abs(...)`.
Addition/subtraction require compatible dimensions. Multiplication/division
derive dimensions: `1kohm * 1uF` is time. Division by zero, overflow, and
nonfinite values are errors. Physical values must have the required dimensions.

```text
param R = 1kohm
param C = 1uF
param tau = R * C
```

Node identifiers are automatically created in component terminal positions.
Queries never create nodes: an unknown queried node is an error. Node and
numeric-parameter namespaces are separate. Single-terminal node warnings can
help find disconnected pins or typos, but topology checks do not prove solvability.

## Components

```text
resistor("R1", positive, negative, 1kohm)
capacitor("C1", positive, negative, 1uF)
voltage_source("V1", positive, negative, 10V)
current_source("I1", positive, negative, 2mA)
diode("D1", anode, cathode, saturation=1pA, ideality=1)
```

Diode saturation and ideality are optional, defaulting to 1 pA and 1.
Resistances, capacitances, diode saturation, and ideality must be positive.
Source values may be signed or zero. Voltage is positive-terminal minus
negative-terminal potential; positive source current flows from the first
terminal to the second. All values must be finite. Duplicate component names
are errors, even across different device kinds within the same instance.

## Analyses and Initial Conditions

```text
dc("bias")
transient("charging", step=100us, stop=5ms)
initial("charging", "C1", 0V)
sweep("curve", source="V1", start=0V, stop=5V, step=100mV)
```

Analysis names are unique. DC selects the nonlinear solver when diodes exist.
Capacitors are open circuits in DC. Transient supports linear circuits only;
diodes cause a compile-time compatibility error. Sources remain constant.
Initial conditions name a capacitor rather than relying on declaration order;
unassigned capacitors start at zero. Duplicate initial assignments are errors.

Backward-Euler transient samples start at the first timestep, not time zero.
The last sample is the largest timestep multiple not beyond stop time. Sweeps
change an existing voltage source; descending sweeps need a negative step.
Each analysis starts from the original circuit. Solver failure/nonconvergence
is an error, not a successful partial result. A script without analyses may be
validated, but cannot be run.

## Observations, Measurements and Checks

```text
observe("bias", voltage(output), voltage(supply, output), current("R1"))
measure("midpoint", from="bias", value=voltage(output))
check("expected output", abs(midpoint - 5V) <= 1mV)
```

`voltage(node)` is relative to ground; the two-node form is differential.
`current("name")` supports voltage-source branch current, independent
current-source current, and resistor current derived from its terminal voltage
difference. Capacitor and diode current queries are not supported in this delivery.
Current sign follows each component's ordered terminal pair.

`observe` requests output traces; it does not execute another solve. DC queries
produce scalars; transient/sweep queries produce series. Queries may be combined
with dimensionally valid arithmetic in measurements. Scalar values broadcast
over a series; series axes must agree.

Measurements must reduce to one scalar. Use `final`, `min`, `max`, or `mean`
on a series:

```text
measure("charged", from="charging", value=final(voltage(output)))
measure("average", from="charging", value=mean(voltage(output)))
check("charged near 10 V", abs(charged - 10V) <= 100mV)
```

`final` uses the last actual solver sample, not an interpolated stop time.
`mean` is arithmetic sample mean, not time-weighted integration. Measurements
may reference previously declared measurements and parameters. Checks use
`<`, `<=`, `>`, `>=`, `==`, or `!=` with matching dimensions. Equality is exact;
use explicit absolute tolerances for numerical results. All completed checks
are reported, not just the first failure. Unreduced series comparisons are errors.

## Reusable Subcircuits

```text
subcircuit divider(input, output, return_node, R=1kohm) {
    resistor("R1", input, output, R)
    resistor("R2", output, return_node, R)
}
voltage_source("V1", supply, ground, 10V)
use("D1", divider, input=supply, output=midpoint, return_node=ground, R=2kohm)
dc("bias")
observe("bias", voltage(midpoint), current("D1.R1"))
```

Ports require explicit named bindings. Parameters have defaults and optional
named overrides with matching dimensions. Subcircuits can contain components,
local parameters, and other instances, but not analyses, observations, or checks.
They do not capture outer numeric parameters. Internal nodes are local to the
instance; built-in ground is global. Flattened names such as `D1.R1` preserve
hierarchy in diagnostics. Query an internal node using a quoted path, for
example `voltage("D1.internal")`.

Recursive instances, missing/extra ports, duplicate names, and excessive
expansion are rejected. Unused templates are also validated with their defaults.
Subcircuits are same-file only; there are no external imports.

## CLI Output and Exit Codes

```bash
./build/microspice_script run examples/scripts/diode_bias.msim --analysis curve
./build/microspice_script run examples/scripts/diode_bias.msim --analysis curve --csv curve > curve.csv
```

`--analysis` selects one analysis. Dependent measurements/checks whose required
analyses were not selected are explicitly skipped. `--csv` chooses CSV output
for a named analysis; by itself it does not restrict which analyses run. If both
options are given, their names must match. CSV contains the requested observations.
Warnings and check reports go to stderr, keeping stdout usable as data.

| Exit | Meaning |
| --- | --- |
| 0 | Successful validation/run and all evaluated checks pass |
| 1 | Compile error or simulation failure |
| 2 | Invalid invocation, input/output error, or unknown selected analysis |
| 3 | One or more evaluated checks fail |
| 130 | Interrupted/cancelled |

Errors include source filename, line/column and a caret. Ctrl+C cancels the CLI.
Cancellation is cooperative, not instantaneous. Scripts cannot open files,
write exports, execute processes, or access the network; the CLI controls I/O.

## Examples

- [Ohm's law resistor load](../examples/scripts/resistor_load.msim)
- [Current-driven resistor](../examples/scripts/current_driven_resistor.msim)
- [Voltage divider](../examples/scripts/voltage_divider.msim)
- [RC charging and waveform measurements](../examples/scripts/rc_charging.msim)
- [Diode bias and voltage sweep](../examples/scripts/diode_bias.msim)
- [Two isolated reusable subcircuit instances](../examples/scripts/subcircuit_divider.msim)

## Limits and Implementation

Default limits: 1 MiB source, nesting depth 32, 1000 elaborated components,
512 MNA unknowns, eight analyses, 2000 total retained samples, and a conservative
four-million-scalar diagnostic budget. Scalar accounting is not an exact memory
byte cap. Trusted C++ callers may supply a `Limits` object. Script contents
cannot bypass these limits.

The Qt-free header-only API is in `microspice::script`:

```cpp
auto program = microspice::script::compile(source);
auto results = microspice::script::run(program, executionControl);
```

Compilation produces a `CompiledSimulation` with a core circuit, named requests,
source maps and warnings. Execution retains native solver diagnostics and typed
observations. No solvers or legacy netlist syntax are replaced.

There are no arbitrary loops, user-defined functions, external imports,
op-amp/transistor/inductor models, AC analysis, or nonlinear transients.
Automatic schematic layout and text/visual round-tripping are not implemented.