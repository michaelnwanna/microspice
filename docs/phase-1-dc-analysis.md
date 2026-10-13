# Phase 1: From KCL to Code

This chapter derives the exact system assembled by `solveDc`. No circuit theory
is hidden behind the API.

## 1. Choose the unknowns

Voltage is always measured relative to another point. Circuit simulators choose
one node as the reference and call it ground:

```text
V(ground) = 0 V
```

Every other node voltage is unknown. For nodes `in` and `out`, the unknown
vector is:

```text
v = [V(in), V(out)]^T
```

Leaving ground out is important. Its voltage is already known, so solving for
it would add a redundant equation and make the matrix singular.

## 2. Write KCL at each node

Consider the walkthrough circuit:

```text
I1: 1 mA from ground to in
R1: 1 kohm from in to out
R2: 1 kohm from out to ground
```

At `in`, the current through `R1` must equal the injected source current:

```text
(V(in) - V(out)) / R1 = I1
```

At `out`, current through `R1` equals current through `R2`:

```text
(V(out) - V(in)) / R1 + V(out) / R2 = 0
```

## 3. Replace resistance with conductance

Conductance is `g = 1/R`. Both 1 kohm resistors therefore have:

```text
g = 1 / 1000 = 0.001 siemens
```

Rearranging the KCL equations gives:

```text
 0.001 V(in) - 0.001 V(out) = 0.001
-0.001 V(in) + 0.002 V(out) = 0
```

Or in matrix form:

```text
[ 0.001  -0.001 ] [ V(in)  ]   [ 0.001 ]
[-0.001   0.002 ] [ V(out) ] = [ 0.000 ]
```

Solving gives `V(in) = 2 V` and `V(out) = 1 V`.

## 4. Stamp components instead of deriving the whole circuit

Real circuits may contain thousands of devices. A simulator does not manually
derive a new symbolic equation for each schematic. Every device contributes a
small, reusable pattern called a **stamp**.

For a resistor between non-ground nodes `a` and `b`:

```text
G[a][a] += g
G[b][b] += g
G[a][b] -= g
G[b][a] -= g
```

For a resistor from `a` to ground, only one term remains:

```text
G[a][a] += g
```

For current `I` flowing from `a` to `b`:

```text
i[a] -= I
i[b] += I
```

If either endpoint is ground, that endpoint's operation is omitted. The source
in the walkthrough flows from ground to `in`, so it contributes `+0.001` to the
`in` row.

## 5. Solve with partial pivoting

Gaussian elimination converts the matrix to upper-triangular form, followed by
back substitution. `solveLinearSystem` uses partial pivoting: before eliminating
a column, it moves the row with the largest absolute coefficient into the pivot
position.

Pivoting matters for two reasons:

1. A valid system may have zero in the current diagonal position.
2. Dividing by a very small coefficient magnifies floating-point error.

If no usable pivot exists, the matrix is singular. In circuit terms, a common
cause is a floating network with no DC path to ground. The solver reports that
condition instead of producing meaningless voltages.

## 6. Inspect, then trust

`DcResult` contains:

- node ordering,
- the assembled conductance matrix,
- the current-injection vector,
- the solved voltages, and
- a text trace of every component stamp.

This is more data than a minimal solver needs, intentionally. It allows tests,
the CLI, and a future GUI to answer not only "what is the voltage?" but also
"which physical component contributed each coefficient?"