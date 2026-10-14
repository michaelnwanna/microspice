# GUI Illustration Sources

The screenshots and animated GIFs are captured from microspice's real Qt Widgets
application, not mockups. The editing GIF uses mouse events to drag a component
with its placement tool still active, reruns DC, then undoes the move. The waveform
GIF moves the plot cursor across a solved 50-sample RC charging trace. Frame timing
is chosen for readability; it is not a measurement of analysis performance.

The Script Workspace GIF edits the RC starter resistance from 1kohm to 900ohm,
validates the source, runs the analysis, shows the successful voltage check,
and switches to the resulting waveform. It uses the real editor and solver.

## Regenerate

From the repository root, configure/build the GUI, then use a separate writable
capture directory. Capture mode ignores saved layouts and never writes settings.

```bash
cmake --build build-gui --target microspice_gui
cmake -E make_directory build-gui/gui-media-captures
cd build-gui/gui-media-captures
../gui/microspice_gui.app/Contents/MacOS/microspice_gui --capture-demo
cd ../..
python3 -m venv build-gui/media-tools
build-gui/media-tools/bin/pip install -r tools/requirements-media.txt
build-gui/media-tools/bin/python tools/render_gui_media.py build-gui/gui-media-captures
```

The executable path above is for macOS. On Windows/Linux use the configured GUI
executable instead. `QT_QPA_PLATFORM=offscreen` also works for deterministic
headless captures. An installed macOS bundle needs a native display because the
deployment omits the offscreen plugin.

The encoder exports three optimized PNGs and two looping GIFs. GIFs share one
palette across frames and use delta compression. PNGs are at most 1100 x 800;
GIFs are at most 1000 x 800. No decorative framing, fabricated cursor overlays,
or simulated solver output is added.

## Script Workspace Captures

The dedicated GUI test also verifies the edited example's successful check:

```bash
cmake --build build-gui --target microspice_script_workspace_test
cd build-gui
QT_QPA_PLATFORM=offscreen ./gui/microspice_script_workspace_test --capture
cd ..
build-gui/media-tools/bin/python tools/render_gui_media.py build-gui --script-only
```

On Windows, set `QT_QPA_PLATFORM` in the shell environment and use the test's
`.exe` path. This adds one optimized PNG and one looping GIF to the gallery.