# Browser Simulator Workspace

The web workspace edits visual `.microspice` schematics and `.msim` source using the same C++17
compiler, DC/nonlinear DC, transient and source-sweep solvers as the desktop
and CLI. It is a local web application, not a second numerical implementation.

Schematic is the initial view, with a working voltage divider. Script is an
independent document: changing modes does not replace either document or
convert between text and drawings. Mode switches cancel pending work and clear
output so results cannot be mistaken for the other document's analysis.

## Build and Start

Requirements: CMake 3.20+, C++17 compiler, Python 3.9+, and a modern browser with
JavaScript, canvas, Fetch and TextDecoder. The first CMake configure downloads
SHA256-pinned nlohmann/json 3.11.3 (MIT) for the optional adapter. Ordinary
core and Qt builds are unchanged. There are no Python packages or npm packages.

```bash
cmake -S . -B build-web -DMS_BUILD_WEB=ON -DMS_BUILD_GUI=OFF
cmake --build build-web --parallel 4
ctest --test-dir build-web --output-on-failure
python3 web/server.py --port 8765
```

Visit http://127.0.0.1:8765. The server binds only to IPv4 loopback. Use another
port if occupied. `--bridge path/to/microspice_web_bridge` selects another
build; on Windows include `.exe` and the configuration folder if applicable.
Stop the server with Ctrl+C. After building, UI assets, examples and help are
served locally without a CDN or network dependency.

## Files and Drafts

In Schematic, Examples offers the five desktop starter projects. Open accepts
UTF-8 `.microspice` files up to 16 MiB using the desktop's version-1 JSON format.
The browser and backend validate references, finite values, 20-unit grid
coordinates, rotations, counters and orthogonal wires before replacement.
Empty projects can be opened without a ground; analysis requires ground.
Download produces a project that the desktop can open without format conversion.
Project files contain the drawing, values and reference counters, not analysis
settings, undo history or computed results.

Use Examples to load any of the six script starters, or Open to load a local
UTF-8 `.msim` file. Sources are limited to 1 MiB. Invalid UTF-8 and oversized
files are rejected without replacing the current source. New/Open/example
replacement checks unsaved edits.

Download saves the active document using a browser download. It cannot overwrite
the original file directly; follow the browser's download behavior. Successful
download marks the in-app document unmodified, but the app cannot verify whether
the browser actually retained the file. Store important work explicitly.

A separate local draft for each mode is restored in the same browser/origin when localStorage is
available. Drafts are not project files, are not synced, and may be lost by
clearing storage or changing ports. Storage failures are shown rather than
blocking editing. Schematic drafts also retain up to 100 undo/redo commands.
Closing/reloading with unsaved work in either mode prompts where supported
by the browser, including an inactive document. Native textarea undo applies
to script edits. Analysis settings are session-local and are not stored in drafts.

## Visual Schematic Editing

Select R, C, D, V, I or G to place a resistor, capacitor, diode, voltage source,
current source or ground. Click an empty grid position to place; dragging an
existing component moves it even when a placement tool is active. Select a
component to edit its value, rotate, or delete it. Ground has no editable value.
Diodes expose saturation current and ideality. Values accept SI suffixes with
case-sensitive `M` for mega and `m` for milli; `u` is micro. Invalid property
text stays visible with an inline error and does not change the model.

Wire connects two snapped grid points, inserting an orthogonal elbow if needed.
Escape cancels a pending wire and returns to Select. Select a segment to delete
it. Endpoint-to-wire and terminal-to-wire touches connect electrically; an
interior-only crossing does not. Explicit junctions are marked. Ground symbols
all share node zero. Source and diode terminal polarity follows the desktop.

Moving or rotating connected components extends orthogonal leads to the old
anchors. The editor checks that the terminal and wire-net connection partition
is unchanged. A move that cannot be routed safely is rejected and rolled back,
including its undo state; clear the destination or try a smaller move. Routing
is not obstacle-aware and old anchors/extensions are not automatically cleaned
up. Very large edits can be rejected by the interactive routing-work budget.

Undo/Redo cover complete schematic edits, including connected wires. Fit frames
the drawing; use zoom controls, wheel zoom, or Pan to inspect it. The editor is
an SVG diagram with responsive dimensions, not a port of Qt rendering.

## Compile and Run

In Script, Compile validates syntax, references, dimensions, model compatibility and limits
without solving. Diagnostics include source locations; selecting one focuses
the source. Compile populates the named-analysis selector. Run executes all
analyses or one selected name. Dependencies outside a selected run are skipped
explicitly. Results include observations, measurements and PASS/FAIL checks.
A failed check is not a compiler or solver failure.

In Schematic, choose DC, Transient, or Sweep. Transient accepts timestep, stop
time, and initial capacitor voltages by reference. Sweep selects an existing
voltage source and accepts start, stop and step in volts. The browser controls
use positive steps and increasing ranges. Compile extracts the circuit and
validates the selected analysis without solving. Run uses the shared compiler
and solvers; output analysis is named `schematic`. All node voltages, ground,
and independent source currents are observed. Node names are assigned by
connectivity rather than screen labels. DC voltages are listed in Results;
in-canvas voltage annotations are not implemented.
Diode transient remains unsupported. Transient and sweep are limited to 2,000
retained samples, including the initial sweep point.

Stop terminates the active solver subprocess. Editing or replacing either document, changing analysis settings, or changing modes clears
old results and cancels pending work; stale responses are not published. One
solver job runs at a time per server. A second browser may receive a busy error.
Jobs time out after 20 seconds; recent jobs expire after ten minutes and only
32 are retained. The compiler's source/model/sample/matrix budgets still apply.
Cancelled jobs retain the solver slot until their worker exits. Schematics can
contain up to 10,000 components and 50,000 wire segments for file compatibility;
simulation is capped at 1,000 components, 2,000 wires, and 512 MNA unknowns before
expensive extraction/solving. HTTP request bodies are capped at 20 MiB; project
JSON is capped at 16 MiB when serialized as compact UTF-8.

## Results and Export

Results shows scalar DC observations, measurements, checks and skipped work.
Select Waveform to inspect a transient or source-sweep trace. Use the analysis
and trace selectors, cursor, zoom controls, wheel zoom, drag pan, and Fit.
Samples lists retained solver points for the selected trace. DC observations
remain scalar; no artificial time series is created.

CSV downloads contain selected observations for the selected successful analysis
and are generated by the same exporter as the CLI. Source downloads do not
contain computed results. Waveforms follow existing solver semantics: transient
samples begin at one timestep, not zero, and reductions use retained samples.

## Keyboard and Help

- Ctrl/Cmd+S: download the active document.
- Ctrl/Cmd+O: open the active mode's file type.
- Ctrl/Cmd+F: fit the schematic or search script source.
- Ctrl/Cmd+Z / Shift+Z: schematic undo/redo; scripts use native editing undo.
- Schematic canvas: arrows move selected components, R rotates, Delete removes,
  Escape cancels a wire and returns to Select.
- F5: run the active mode.
- Plot arrows: move the sample cursor; +/-: zoom; Home: fit.

Help opens workspace guidance and links to locally bundled Markdown references.
The [language reference](simulation-language.md) defines supported syntax;
the [desktop guide](user-guide.md) applies to Qt, not browser-specific controls.

## Security and Deployment Boundary

Scripts and projects are sent to the local Python service and are not uploaded to a remote
service. The bridge has no script-initiated file/network/process API. The server
checks Host and Origin, has no CORS allowance, serves only explicitly mapped
assets/docs/examples, caps request bodies and uses subprocess argument lists
without a shell. Solver output is returned as JSON; the UI inserts source-derived
labels as text. Cancellation kills the child process; it does not alter files.

This server is intended for one trusted local user. It has no authentication,
TLS, multi-user isolation or production sandbox. Do not expose it through a
reverse proxy, public tunnel, or remote bind. OS subprocess execution is not a
security sandbox. Browser extensions and other local processes are outside
its protection boundary.

## Current Limitations

The browser does not expose matrix/Newton inspectors, convert scripts to
schematics, run without the local C++ bridge, or provide WebAssembly/offline service-worker
deployment. Source syntax is not highlighted yet. There is no AC analysis,
nonlinear transient support, inductors, or additional device models. Browser
schematic changes are local implementation work until hosted validation is run.

## Tests

`microspice_web_tests` exercises all six starter scripts through the real bridge,
DC parity, source diagnostics, named analysis selection, CSV availability, RC
sample counts, HTTP job lifecycle, cancellation, busy handling, source limits,
Host/Origin guards and static path restrictions. Project tests also cover the
five desktop examples, signed/rotated source terminals, malformed projects,
connectivity crossings/junctions, limits, RC initial states and diode sweeps.

With the server running, open
http://127.0.0.1:8765/tests/schematic.test.html?integration for the executable
browser harness. It exercises real-backend routing preservation, rejected
moves, history, independent documents, stale-job handling, sweep and sample
boundaries. No mock solver data is used. Desktop/mobile screenshots and live
interaction checks complement this harness; physical touch and other browser
engines still need separate testing.

For direct parity with the Qt project parser and geometry adapter:

```bash
cmake -S . -B build-gui -DMS_BUILD_GUI=ON -DMS_BUILD_WEB=ON
cmake --build build-gui --parallel 4
ctest --test-dir build-gui --output-on-failure
```

Use the Qt prefix/SDK options required by your machine. The optional
`microspice_web_project_parity_test` checks desktop JSON round trips and exact
circuit terminal/node parity. Qt is not linked into the web bridge itself.