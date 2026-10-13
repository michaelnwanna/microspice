# microspice

> A SPICE-inspired circuit simulator built from first principles in modern C++.
> The goal is not only to compute voltages, but to make every equation, matrix
> stamp, and numerical decision visible.

[![CI](https://github.com/michaelnwanna/microspice/actions/workflows/ci.yml/badge.svg)](../../actions/workflows/ci.yml)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![Phase](https://img.shields.io/badge/phase-1%20DC%20analysis-teal)

## Current phase: linear DC analysis

Phase 1 solves resistor and independent current-source circuits with **nodal
analysis**. It includes:

- a dense matrix type,
- Gaussian elimination with partial pivoting,
- a GUI-independent circuit model,
- resistor and current-source matrix stamps,
- detection of floating or otherwise singular circuits,
- an inspectable result containing the matrix, right-hand side, stamps, and
  node voltages,
- dependency-free tests derived from Ohm's law, and
- a CLI walkthrough that prints the complete solve.

The deliberately small component set keeps the first milestone honest: before
adding a parser, capacitors, diodes, or graphics, the linear algebra and circuit
equations must be correct and understandable.

## Quick start

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/dc_walkthrough
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

## Library API

The simulation core does not depend on terminal or GUI code:

```cpp
#include "microspice/circuit.hpp"
#include "microspice/dc_solver.hpp"

microspice::Circuit circuit;
circuit.addCurrentSource("I1", "0", "in", 1e-3);
circuit.addResistor("R1", "in", "out", 1e3);
circuit.addResistor("R2", "out", "0", 1e3);

const auto result = microspice::solveDc(circuit);
const double outputVoltage = result.voltage("out");
```

`DcResult` also retains the assembled matrix, current vector, and every stamp.
That data is useful for tests today and can power a matrix inspector in the
future GUI without coupling the solver to Qt.

## Architecture

```text
future Qt GUI        CLI examples        future netlist parser
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

This is not a replacement for production SPICE software. Phase 1 supports no
ideal voltage sources, capacitors, inductors, controlled sources, or nonlinear
devices. Those limitations are explicit so each later feature arrives with its
mathematics, tests, and failure modes explained.