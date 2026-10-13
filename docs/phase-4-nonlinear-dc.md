# Phase 4: Nonlinear DC Analysis

Linear MNA works when every device can be represented by constant matrix and
right-hand-side entries. A diode is different: its current changes
exponentially with its terminal voltage. Phase 4 solves that nonlinear system
by repeatedly constructing and solving a linear approximation.

## The Shockley diode model

For voltage $V_D=V(anode)-V(cathode)$, microspice uses:

$$
I_D=I_S\left(e^{V_D/(nV_T)}-1\right)
$$

- $I_S$ is saturation current, defaulting to $10^{-12}$ A.
- $n$ is the ideality factor, defaulting to 1.
- $V_T$ is thermal voltage, defaulting to 25.852 mV.

The derivative is the diode's small-signal conductance at that voltage:

$$
G_D=\frac{dI_D}{dV_D}
    =\frac{I_S}{nV_T}e^{V_D/(nV_T)}
$$

Both quantities are available through `linearizeDiode`, which keeps the device
equation independently testable from matrix assembly.

## Tangent linearization

At voltage guess $V_k$, Newton-Raphson replaces the nonlinear curve with its
tangent:

$$
I_D(V)\approx I_D(V_k)+G_D(V_k)(V-V_k)
$$

Rearranging gives a form suitable for MNA:

$$
I_D(V)\approx G_DV+I_{eq}
$$

where

$$
I_{eq}=I_D(V_k)-G_DV_k
$$

The conductance receives the ordinary four-entry resistor stamp. The equivalent
current source points from anode to cathode, so its right-hand-side stamp is:

```text
z[anode]   -= Ieq
z[cathode] += Ieq
```

Ground terms are omitted as usual. Solving the resulting linear system produces
a new junction voltage, and the process repeats.

## Why voltage limiting is necessary

Consider a 5 V source, 1 kohm resistor, and diode. Starting from 0 V makes the
diode look almost open, so the first linear solve proposes nearly 5 V across it.
Directly evaluating $e^{5/V_T}$ would produce an enormous value and destabilize
the next solve.

microspice applies the classic SPICE junction-voltage limiting transformation
before evaluating the next tangent. It permits large safe progress when far
from the answer and ordinary Newton steps near convergence. The educational
walkthrough shows the limited guesses progressing approximately through
0.136 V, 0.272 V, 0.406 V, and 0.535 V before converging near 0.574 V.

## Convergence and diagnostics

An operating point is accepted when all conditions are below their configured
tolerances:

- maximum change in any node voltage since the previous iteration,
- maximum change in any voltage-source branch current,
- maximum difference between solved diode voltage and tangent voltage.

`NonlinearDcResult` reports whether convergence occurred and retains every
`NewtonIteration`. Each iteration stores its maximum changes plus every diode's
linearization voltage, solved voltage, current, conductance, and equivalent
current. Reaching the iteration limit returns `converged=false` with those
diagnostics intact.

Run the iteration table with:

```bash
./build/nonlinear_walkthrough
```

## Netlist syntax

A diode record names its anode and cathode. Model parameters are optional:

```text
Dname anode cathode [Is [n]]
```

Examples:

```text
D1 out 0
D2 out 0 2p 1.5
```

The first uses default values. The second uses $I_S=2$ pA and $n=1.5$.

## DC source sweeps

The directive

```text
.dc source start stop step
```

solves one nonlinear operating point at each source value. For example:

```text
V1 out 0 0
D1 out 0
.dc V1 0 0.8 0.02
```

The CLI writes CSV with the swept voltage, all node voltages, all independent
voltage-source currents, convergence status, and Newton iteration count:

```bash
./build/microspice_cli examples/diode_curve.cir > diode.csv
```

`diode_clipper.cir` demonstrates antiparallel limiting. The positive and
negative sweeps settle near one diode drop. `half_wave_rectifier.cir` shows a
series diode blocking negative input and delivering positive input to a load
after its forward drop.

## Current boundary

Phase 4 applies nonlinear iteration to DC operating points and DC sweeps.
Transient analysis deliberately rejects circuits containing diodes rather than
silently ignoring them. Combining capacitor companion models with Newton
iterations at every timestep is a later extension, after the GUI can make the
existing DC convergence behavior visible.