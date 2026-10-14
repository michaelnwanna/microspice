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

## Browser Workspace Captures

The browser gallery is captured from the running visual web workspace and its
real C++ bridge. The divider DC result is 5 V at the midpoint. The RC transient
uses R1 = 1kohm, C1 = 1uF, a 100us step, a 5ms stop, and zero initial capacitor
voltage. Its 50 samples charge toward 10 V; the pictured cursor reads
9.369606 V at 2.9ms. No output, overlays, or circuit graphics are fabricated.

Start the service using the repository's Browser Workspace build instructions:

```bash
python3 web/server.py --port 8765
```

In another terminal at the repository root, prepare capture dependencies:

```bash
python3 -m venv build-web/media-tools
build-web/media-tools/bin/pip install -r tools/requirements-media.txt playwright
```

The following reproducible capture session uses an installed Google Chrome in
a visible window (`channel="chrome", headless=False`). This avoids viewport
resizing by the integrated browser UI and headless screenshot incompatibilities
on the capture machine. Alternatively, install Playwright Chromium with
`build-web/media-tools/bin/python -m playwright install chromium` and use
`runner.chromium.launch(headless=False)`.

```bash
build-web/media-tools/bin/python - <<'PY'
from pathlib import Path
from playwright.sync_api import sync_playwright, expect

captures = Path("build-web/browser-media")
captures.mkdir(parents=True, exist_ok=True)
with sync_playwright() as runner:
	browser = runner.chromium.launch(channel="chrome", headless=False)
	page = browser.new_page(viewport={"width": 1440, "height": 1000},
							device_scale_factor=1)
	page.goto("http://127.0.0.1:8765/")
	expect(page.get_by_text("Engine ready", exact=True)).to_be_visible()

	def capture(name):
		page.set_viewport_size({"width": 1440, "height": 1000})
		assert page.evaluate("[innerWidth, innerHeight]") == [1440, 1000]
		page.screenshot(path=str(captures / name), scale="css",
						clip={"x": 0, "y": 0, "width": 1440, "height": 1000})

	def example(label, filename):
		page.get_by_role("combobox", name="Project examples").select_option(label=label)
		expect(page.get_by_text("Opened " + filename, exact=True)).to_be_visible(timeout=15000)

	def run():
		page.get_by_role("button", name="Run", exact=True).evaluate("button => button.click()")
		expect(page.get_by_text("Run complete.", exact=True)).to_be_visible(timeout=30000)

	example("Voltage Divider", "voltage_divider.microspice")
	run()
	expect(page.get_by_role("cell", name="5 V", exact=True)).to_be_visible()
	capture("web-schematic.png")
	example("Rc Charging", "rc_charging.microspice")
	capture("demo-browser-00.png")
	page.get_by_role("combobox", name="Analysis", exact=True).select_option(label="Transient")
	page.get_by_role("textbox", name="Stop (s)", exact=True).fill("5m")
	page.get_by_role("textbox", name="Step (s)", exact=True).fill("100u")
	page.get_by_role("textbox", name="C1 initial (V)", exact=True).fill("0")
	page.get_by_role("textbox", name="C1 initial (V)", exact=True).blur()
	capture("demo-browser-01.png")
	run()
	capture("demo-browser-02.png")
	page.get_by_role("tab", name="Waveform", exact=True).evaluate("tab => tab.click()")
	page.get_by_role("combobox", name="Trace", exact=True).select_option(label="voltage(n2) (V)")
	expect(page.get_by_role("status", name="50 samples", exact=True)).to_be_visible()
	capture("demo-browser-03.png")
	bounds = page.locator("#plot").bounding_box()
	page.mouse.move(bounds["x"] + bounds["width"] * 0.6,
					bounds["y"] + bounds["height"] * 0.4)
	expect(page.locator("#cursor")).to_contain_text(" V")
	capture("demo-browser-04.png")
	capture("web-transient.png")
	browser.close()
PY
build-web/media-tools/bin/python tools/render_gui_media.py build-web/browser-media --browser-only
```

Frames 00 through 04 show the opened RC example, transient settings, successful
run results, capacitor-node waveform, and cursor readout, respectively. All
seven source PNGs must have equal dimensions; use 1440 x 1000 for this gallery.
The browser-only encoder leaves desktop media untouched and writes
`web-schematic.png`, `web-transient.png`, and `web-workflow.gif` here. The PNGs
are at most 1100 x 800 and the GIF at most 1000 x 800, with five equally sized
frames and infinite looping. It decodes every generated GIF frame to validate
the result. Playback pauses are for readability, not solver timing.