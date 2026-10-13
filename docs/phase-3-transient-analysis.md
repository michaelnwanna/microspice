# Phase 3: Transient Analysis

DC analysis answers what happens after all transients settle. A capacitor makes
time part of the problem because its current depends on how quickly its voltage
changes:

$$
i_C(t) = C\frac{dv_C(t)}{dt}
$$

microspice advances time in fixed steps and uses backward Euler to convert this
differential relation into one linear MNA system per step.

## From a derivative to a companion model

Let $v_n$ be the capacitor voltage at the new time and $v_{n-1}$ its voltage at
the previous time. Backward Euler approximates the derivative at the new time:

$$
\frac{dv}{dt}\bigg|_n \approx \frac{v_n-v_{n-1}}{\Delta t}
$$

Substitution gives:

$$
i_n = \frac{C}{\Delta t}v_n-\frac{C}{\Delta t}v_{n-1}
$$

Define the equivalent conductance and history current:

$$
G_{eq}=\frac{C}{\Delta t}, \qquad I_{history}=G_{eq}v_{n-1}
$$

The capacitor is now represented during one step by a conductance $G_{eq}$ in
parallel with a history source. Its conductance uses the same four matrix terms
as a resistor. For a capacitor from positive node `p` to negative node `n`, the
history contribution is:

```text
z[p] += I_history
z[n] -= I_history
```

After solving, `V(p)-V(n)` is saved for the next step. This state update is why
a transient simulation is a sequence of related linear solves rather than one
large solve.

## Worked RC charging circuit

The example network is:

```text
              R = 1 kohm
  10 V  (in) ----/\/\/---- (out)
    |                         |
  ground                   C = 1 uF
                              |
                           ground
```

Its time constant is:

$$
\tau=RC=(1000)(10^{-6})=1\text{ ms}
$$

Starting from 0 V, the exact output is:

$$
v_{out}(t)=10\left(1-e^{-t/\tau}\right)
$$

With $\Delta t=1\text{ ms}$, $G_{eq}=1\text{ mS}$. During the first step the
history source is zero, so the resistor and capacitor companion conductance form
an equal divider. Backward Euler therefore predicts exactly 5 V at the first
sample. The analytical value at 1 ms is about 6.32 V; reducing the timestep
reduces this numerical error.

Backward Euler is first-order accurate and introduces numerical damping, but it
is robust for stiff circuit equations. Fixed timesteps keep this phase small and
make every calculation reproducible. Adaptive integration is reserved for a
later phase.

## Netlist and CSV workflow

The RC example is expressed as:

```text
V1 in 0 10
R1 in out 1k
C1 out 0 1u
.tran 100u 5m
.end
```

Run it and save the waveform with:

```bash
./build/microspice_cli examples/rc_charging.cir > rc.csv
```

The first row names time and every non-ground node. Each later row is one solved
sample beginning at $t=\Delta t$:

```text
time_s,V(in),V(out)
0.0001,10,0.909090909091
```

`transient_walkthrough` prints $G_{eq}$, previous voltage, history current,
simulated voltage, analytical voltage, and error at selected points.

## Core result model

`TransientResult` owns ordered `TransientSample` values. Each sample retains:

- simulation time,
- the assembled MNA matrix,
- the right-hand side and solved unknown vector, and
- each capacitor's previous voltage, equivalent conductance, history current,
  and solved voltage.

This is intentionally richer than a voltage-only waveform. A future GUI can
plot node voltages while also explaining the exact companion model and matrix
that produced any selected point, without duplicating solver logic.