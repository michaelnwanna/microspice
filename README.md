# microspice

> A SPICE-inspired circuit simulator built from first principles in modern C++.
> The goal is not only to compute voltages, but to make every equation, matrix
> stamp, and numerical decision visible.

[![CI](https://github.com/michaelnwanna/microspice/actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Phase](https://img.shields.io/badge/phase-2%20MNA%20%2B%20netlists-teal)

## Current phase: netlists and Modified Nodal Analysis

microspice solves linear DC circuits containing resistors and independent current
and voltage sources. Phase 2 includes:

- a dense matrix type,
- Gaussian elimination with partial pivoting,
- a GUI-independent circuit model,
- Modified Nodal Analysis (MNA) with inspectable component stamps,
- ideal voltage sources and their solved branch currents,
- a strict SPICE-like netlist parser with line-numbered errors,
- engineering values such as `1k`, `4.7u`, and `2meg`,
- detection of floating or otherwise singular circuits,
- an inspectable result containing the full MNA system and solution,
- dependency-free tests derived from Ohm's law, and
- CLI tools for programmatic and text-netlist workflows.

The component set remains deliberately small. Capacitors and diodes arrive only
after the linear DC foundation and its text boundary are testable and visible.

## Quick start

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/dc_walkthrough
./build/microspice_cli examples/voltage_divider.cir
```

The example constructs this network:

```text
             R1 = 1 kohm       R2 = 1 kohm
  1 mA --->  (in) ---/\/\/--- (out) ---/\/\/--- ground
```

The same current passes through both resistors, so a hand calculation gives:

```text
V(out) = 1 mA * 1 kohm = 1 V
V(in)  = 1 mA * 2 kohm = 2 V
```

The program then shows how the simulator reaches those values from a matrix.

The netlist CLI uses the more familiar voltage-divider example:

```text
* A 10 V source and two equal resistors produce 5 V at out.
V1 in 0 DC 10
R1 in out 1k
R2 out 0 1k
.end
```

It reports `V(in) = 10 V`, `V(out) = 5 V`, and `I(V1) = -5 mA`. Source current
is defined from the source's `+` node to its `-` node, so the negative value says
the source is delivering power to the resistors.

## The central equation

Kirchhoff's Current Law says that currents at every node must balance. For
linear DC circuits, collecting one equation per non-ground node produces:

```text
G * v = i
```

- `G` is the conductance matrix.
- `v` is the vector of unknown node voltages.
- `i` is the vector of current injected into each node.
- Ground is known to be 0 V, so it is not an unknown or matrix row.

A resistor with resistance `R` has conductance `g = 1/R`. Between nodes `a`
and `b`, it stamps:

```text
G[a][a] += g
G[b][b] += g
G[a][b] -= g
G[b][a] -= g
```

Terms involving ground are omitted because ground has no matrix row. A current
source whose arrow runs from `a` to `b` removes current from `a` and injects it
into `b`:

```text
i[a] -= I
i[b] += I
```

See [Phase 1: From KCL to Code](docs/phase-1-dc-analysis.md) for the complete
derivation and a worked matrix example.

## Why voltage sources require MNA

An ideal voltage source states a voltage directly but does not provide a
current-resistance relationship. Ordinary nodal analysis therefore has no way
to write its current in terms of node voltages. Modified Nodal Analysis adds one
unknown branch current and one voltage equation for each ideal source:

```text
A * x = z

x = [node voltages, voltage-source currents]
```

For `V1 in 0 10`, the added equation is `V(in) = 10`, and the added current
unknown participates in KCL at `in`. This turns the two-node divider into a
three-unknown system: `V(in)`, `V(out)`, and `I(V1)`.

See [Phase 2: MNA and Netlists](docs/phase-2-mna-netlists.md) for the augmented
matrix derivation, source stamp, parser grammar, suffix table, and sign
conventions.

## Library API

The simulation core does not depend on terminal or GUI code:

```cpp
#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"

microspice::Circuit circuit;
circuit.addVoltageSource("V1", "in", "0", 10.0);
circuit.addResistor("R1", "in", "out", 1e3);
circuit.addResistor("R2", "out", "0", 1e3);

const auto result = microspice::solveDc(circuit);
const double outputVoltage = result.voltage("out");
```

`DcResult` also retains the assembled matrix, right-hand side, complete solution,
source-current names, and every stamp. That data is useful for tests today and
can power a matrix inspector in the future GUI without coupling the solver to
Qt.

## Architecture

```text
future Qt GUI        CLI examples          netlist parser
      |                   |                       |
      +-------------------+-----------------------+
                          |
                    public Circuit API
                          |
             circuit model + analyses
                          |
              matrix assembly + solver
```

The dependency direction always points toward the numerical core. A schematic
editor will translate symbols and wires into a `Circuit`; it will never contain
the simulation equations itself.

## Development phases

### Phase 1 - Linear DC foundation (complete)

Dense solver, resistor/current-source stamps, traceable matrix assembly, CLI,
and hand-verifiable tests.

### Phase 2 - Netlists and voltage sources

Parse a small SPICE-like text format, support engineering suffixes (`1k`,
`10u`), and introduce Modified Nodal Analysis for ideal voltage sources.
**Complete.**

### Phase 3 - Time-domain simulation

Add capacitors, backward-Euler integration, transient analysis, CSV export, and
RC charging/discharging examples checked against analytical solutions.

### Phase 4 - Nonlinear devices

Add the Shockley diode equation, Newton-Raphson linearization, convergence
diagnostics, DC sweeps, diode curves, clippers, and rectifiers.

### Phase 5 - Desktop GUI

Build a Qt 6 schematic editor with wires, component properties, probes,
waveform plots, undo/redo, project files, and an educational matrix/iteration
inspector. The GUI will consume the same tested core API.

### Phase 6 - Scale and performance

Introduce sparse storage, symbolic ordering, adaptive timesteps, additional
devices, benchmarks, and comparisons against established SPICE implementations.

## What this is not yet

This is not a replacement for production SPICE software. Phase 2 supports no
capacitors, inductors, controlled sources, nonlinear devices, or simulation
directives beyond `.end`. Those limitations are explicit so each later feature
arrives with its mathematics, tests, and failure modes explained.