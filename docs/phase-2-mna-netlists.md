# Phase 2: Modified Nodal Analysis and Netlists

Phase 1 expressed resistor/current-source circuits as `G * v = i`. This chapter
explains why ideal voltage sources need a larger system, derives their stamp,
and follows a text netlist through parsing and simulation.

## 1. The limitation of ordinary nodal analysis

Nodal analysis needs every branch current to be expressible using node voltages.
A resistor satisfies that requirement:

```text
I = (V(a) - V(b)) / R
```

An ideal voltage source instead provides this constraint:

```text
V(a) - V(b) = Vs
```

Its current depends on the surrounding circuit and cannot be calculated from
the source voltage alone. Omitting that current would make the KCL equations
incomplete.

## 2. Add the missing current as an unknown

Modified Nodal Analysis adds one unknown current for each independent voltage
source. For two non-ground nodes and one source:

```text
x = [V(in), V(out), I(V1)]^T
```

It also adds the source's voltage constraint as a new equation. In block form:

```text
[ G  B ] [v] = [i]
[ C  D ] [j]   [e]
```

- `G` contains conductance stamps.
- `B` inserts voltage-source currents into node KCL equations.
- `C` encodes source voltage differences.
- `D` is zero for independent ideal voltage sources.
- `j` contains the new source-current unknowns.
- `e` contains source voltages.

For independent sources, `C` is the transpose of `B`, so the assembled matrix
is symmetric.

## 3. Voltage-source stamp

For source `V1` from positive node `p` to negative node `n`, let `k` be its
extra branch row and column:

```text
A[p][k] += 1
A[k][p] += 1
A[n][k] -= 1
A[k][n] -= 1
z[k]    += Vs
```

As with resistor stamps, operations involving ground are omitted because ground
has no unknown. The first two matrix entries put `I(V1)` into node KCL and
`V(p)` into the source equation. The negative-node entries provide the opposite
sign. The right-hand-side entry completes `V(p) - V(n) = Vs`.

## 4. Worked 10 V divider

The netlist is:

```text
V1 in 0 DC 10
R1 in out 1k
R2 out 0 1k
```

The unknown vector and assembled system are:

```text
x = [V(in), V(out), I(V1)]^T

[ 0.001  -0.001   1 ] [V(in) ]   [ 0 ]
[-0.001   0.002   0 ] [V(out)] = [ 0 ]
[ 1.000   0.000   0 ] [I(V1) ]   [10 ]
```

The third row enforces `V(in) = 10 V`. The second row then gives `V(out) = 5 V`.
The first row gives `I(V1) = -5 mA`.

The source-current direction is defined from its positive node to its negative
node. A negative answer therefore means current actually leaves the positive
terminal: the source is delivering power.

## 5. Netlist grammar

Phase 2 intentionally supports a small grammar:

```text
Rname positive-node negative-node resistance
Iname positive-node negative-node [DC] current
Vname positive-node negative-node [DC] voltage
.end
```

Examples:

```text
Rload out 0 4.7k
Ibias 0 base 20u
Vsupply vcc 0 DC 3.3
```

Lines whose first non-whitespace token begins with `*` or `#` are comments.
Text after `;` is an inline comment. Parsing stops at `.end`.

## 6. Engineering suffixes

Values are converted to SI units before they enter `Circuit`:

| Suffix | Multiplier | Example |
| --- | ---: | ---: |
| `t` | 1e12 | `1t` |
| `g` | 1e9 | `1g` |
| `meg` | 1e6 | `2meg` |
| `k` | 1e3 | `4.7k` |
| `m` | 1e-3 | `3m` |
| `u` | 1e-6 | `20u` |
| `n` | 1e-9 | `10n` |
| `p` | 1e-12 | `5p` |

Like traditional SPICE, suffixes are case-insensitive and `M` means milli;
mega must be written as `meg`. This is subtle enough to deserve an explicit
test.

## 7. Error boundaries

The parser records the current source line. Syntax errors, invalid values, and
component validation failures are all surfaced as `NetlistError` with that line
number. Once parsing succeeds, the solver receives a normal `Circuit`, exactly
as programmatic clients and the future GUI do.

This separation is important: the parser translates text, the circuit model
owns electrical data, and the solver owns equations. No layer reimplements
another layer's responsibility.