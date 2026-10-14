import { SchematicEditor, dividerProject, emptyProject, validateProject, parseEngineering, pins, rerouteAttached } from '../schematic.js';
const report = document.getElementById('report');
const results = [];
const assert = (condition, message) => { if (!condition) throw new Error(message); };
function test(name, check) { try { check(); results.push(`PASS ${name}`); } catch (error) { results.push(`FAIL ${name}: ${error.message}`); } }
function connectivity(project) {
  const points = new Map();
  const key = (point) => point.join(',');
  const root = (node) => { if (!points.has(node)) points.set(node, node); if (points.get(node) !== node) points.set(node, root(points.get(node))); return points.get(node); };
  const join = (first, second) => points.set(root(first), root(second));
  for (const component of project.components) for (const point of pins(component)) root(key(point));
  for (const wire of project.wires) join(key(wire.start), key(wire.end));
  for (const pointKey of [...points.keys()]) {
    const [pointX, pointY] = pointKey.split(',').map(Number);
    for (const wire of project.wires) {
      const [[startX, startY], [endX, endY]] = [wire.start, wire.end];
      if ((startX === endX && pointX === startX && pointY >= Math.min(startY, endY) && pointY <= Math.max(startY, endY)) || (startY === endY && pointY === startY && pointX >= Math.min(startX, endX) && pointX <= Math.max(startX, endX))) join(pointKey, key(wire.start));
    }
  }
  const terminals = project.components.flatMap((component) => pins(component).map((point, index) => [`${component.reference}:${index}`, root(key(point))]));
  return terminals.flatMap(([name, net], index) => terminals.slice(index + 1).filter(([, other]) => other === net).map(([other]) => `${name}=${other}`)).sort().join(';');
}
test('SI engineering, case-sensitive mega/milli and units', () => {
  assert(parseEngineering('2M') === 2e6 && parseEngineering('2m') === .002, 'M/m');
  assert(parseEngineering('4.7kΩ', 'Ω') === 4700 && Math.abs(parseEngineering('10uF', 'F') - 1e-5) < 1e-19, 'units');
  for (const text of ['NaN', 'Infinity', '1e999', '1q', '1V']) { let rejected = false; try { parseEngineering(text); } catch { rejected = true; } assert(rejected, text); }
});
test('exact v1 format and structural limits', () => {
  validateProject(emptyProject()); validateProject(dividerProject());
  for (const mutate of [(project) => project.components[0].position[0] = 1, (project) => project.wires[0].end = [40, 40], (project) => project.components[1].value = 0, (project) => project.components[0].rotation = 45, (project) => project.counters[0] = -1]) {
    const project = dividerProject(); mutate(project); let rejected = false; try { validateProject(project); } catch { rejected = true; } assert(rejected, 'accepted invalid project');
  }
});
test('RC connectivity survives move and rotation, including pin on wire interior', () => {
  for (const kind of ['move', 'rotate', 'junction']) {
    const project = dividerProject(); project.components[2].kind = 1; project.components[2].reference = 'C1'; project.components[2].value = 1e-6;
    if (kind === 'junction') project.wires.push({ start: [180, -120], end: [180, -220] });
    const baseline = connectivity(project), component = project.components[2], before = structuredClone(component);
    if (kind === 'rotate') component.rotation = 90; else component.position = [280, -200];
    rerouteAttached(project, before, component); validateProject(project);
    assert(connectivity(project) === baseline, `${kind}: terminal nets changed`);
  }
});
test('long moves preserve all terminal nets or reject without adding wires', () => {
  for (const position of [[-400, 0], [0, 0], [400, 0], [-400, 120], [400, 120], [800, -400], [-800, -400]]) {
    const project = dividerProject(), baseline = connectivity(project), component = project.components[2], before = structuredClone(component), wires = JSON.stringify(project.wires);
    component.position = position;
    try { rerouteAttached(project, before, component); assert(connectivity(project) === baseline, `short at ${position}`); }
    catch (error) { assert(/rejected/.test(error.message), error.message); assert(JSON.stringify(project.wires) === wires, 'rejected route modified wires'); }
  }
});
test('colliding transforms and drag rollback preserve project, undo and redo', () => {
  const errors = [], editor = new SchematicEditor(document.getElementById('fixture'), () => {}, () => {}, (message) => errors.push(message));
  editor.select({ type: 'component', id: 'R2' }); editor.property('value', 2000); editor.history(); editor.select({ type: 'component', id: 'R2' });
  const before = editor.snapshot(), undo = JSON.stringify(editor.undoStack), redo = JSON.stringify(editor.redoStack);
  let rejected = false;
  try { editor.move(-240, 120); } catch (error) { rejected = /rejected/.test(error.message); }
  assert(rejected && editor.snapshot() === before, 'collision was not rolled back');
  assert(JSON.stringify(editor.undoStack) === undo && JSON.stringify(editor.redoStack) === redo, 'history changed on rejection');
  editor.gesture = { type: 'move', before, component: structuredClone(editor.selectedComponent()) };
  editor.selectedComponent().position = [0, 0]; editor.endGesture();
  assert(editor.snapshot() === before && errors.length === 1, 'drag rollback/error');
  editor.history(true); assert(editor.project.components[2].value === 2000, 'redo lost');
  editor.observer.disconnect(); editor.svg.remove();
});
test('interior crossings are unjoined; explicit endpoints join', () => {
  const editor = new SchematicEditor(document.getElementById('fixture'), () => {});
  const project = emptyProject(); project.wires = [{ start: [-100, 0], end: [100, 0] }, { start: [0, -100], end: [0, 100] }]; editor.replace(project);
  assert(editor.layer.querySelectorAll('.junction').length === 0, 'crossing marker');
  project.wires.push({ start: [0, 0], end: [0, 40] }); editor.replace(project);
  assert(editor.layer.querySelector('.junction[cx="0"][cy="0"]'), 'missing explicit marker');
  editor.observer.disconnect(); editor.svg.remove();
});
test('snapshot undo/redo, collision skipping, 100-command bound, replacement reset', () => {
  const editor = new SchematicEditor(document.getElementById('fixture'), () => {}); editor.fit();
  const before = editor.snapshot(); editor.select({ type: 'component', id: 'R1' }); editor.rotate(); const after = editor.snapshot(); editor.history(); assert(editor.snapshot() === before, 'undo'); editor.history(true); assert(editor.snapshot() === after, 'redo');
  editor.project.counters[0] = 0; editor.setTool(0); editor.point = () => [500, 500]; editor.pointerDown({ button: 0, target: editor.background }); assert(editor.project.components.at(-1).reference === 'R3', 'reference collision');
  for (let index = 0; index < 110; index += 1) editor.edit(() => { editor.project.components[0].value += 1; });
  assert(editor.undoStack.length === 100, 'history bound'); editor.replace(emptyProject()); assert(!editor.undoStack.length && !editor.redoStack.length, 'replacement history'); editor.observer.disconnect(); editor.svg.remove();
});
async function backendJob(body) {
  const response = await fetch('/api/jobs', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
  const created = await response.json(); assert(response.ok && created.id, JSON.stringify(created));
  try {
    for (let attempt = 0; attempt < 200; attempt += 1) {
      const job = await (await fetch(`/api/jobs/${encodeURIComponent(created.id)}`)).json();
      if (job.state === 'done') return job.result;
      assert(job.state === 'running', JSON.stringify(job));
      await new Promise((resolve) => setTimeout(resolve, 50));
    }
    throw new Error('Backend job timed out');
  } finally { await fetch(`/api/jobs/${encodeURIComponent(created.id)}/cancel`, { method: 'POST' }); }
}
if (new URLSearchParams(location.search).has('integration') || location.hash === '#integration') {
  try {
    const examples = await (await fetch('/api/project-examples')).json();
    const project = examples.find((example) => example.id === 'rc_charging').project;
    const settings = { kind: 'transient', step: 100e-6, stop: .005, initialVoltages: { C1: 0 } };
    const baseline = await backendJob({ project, operation: 'run', settings });
    assert(baseline.ok, JSON.stringify(baseline.diagnostics));
    const terminalSignature = (result) => {
      const terminals = Object.entries(result.terminals).flatMap(([name, nets]) => nets.map((net, index) => [`${name}:${index}`, net]));
      return terminals.flatMap(([name, net], index) => terminals.slice(index + 1).filter(([, other]) => other === net).map(([other]) => `${name}=${other}`)).sort().join(';');
    };
    const waveform = (result) => {
      const analysis = result.results[0], net = result.terminals.C1[0];
      assert(analysis.axis.length === 50, 'RC must retain exactly 50 samples');
      const values = analysis.observations.find((observation) => observation.label === `voltage(${net === '0' ? 'ground' : net})`).data;
      assert(Math.abs(values.at(-1) - 9.914814487204984) < 1e-10, `RC final ${values.at(-1)}`);
      return values;
    };
    const expected = waveform(baseline);
    let accepted = 0, rejected = 0;
    for (const change of [{ rotation: 90 }, { rotation: 180 }, { rotation: 270 }, ...[[-400, 0], [0, 0], [400, 0], [800, -400], [280, -200]].map((position) => ({ position }))]) {
      const candidate = structuredClone(project), component = candidate.components.find((item) => item.reference === 'C1'), before = structuredClone(component);
      Object.assign(component, change);
      try { rerouteAttached(candidate, before, component); }
      catch (error) { assert(/rejected/.test(error.message), error.message); rejected += 1; continue; }
      const result = await backendJob({ project: candidate, operation: 'run', settings });
      assert(result.ok, JSON.stringify(result.diagnostics));
      assert(terminalSignature(result) === terminalSignature(baseline), `backend terminal nets changed: ${JSON.stringify(change)}`);
      assert(waveform(result).every((value, index) => Math.abs(value - expected[index]) < 1e-10), 'backend RC waveform changed'); accepted += 1;
    }
    assert(accepted > 0 && rejected > 0, 'must exercise accepted and rejected routes');
    results.push(`PASS real backend RC: 50 samples, terminal nets and waveform unchanged (${accepted} accepted, ${rejected} rejected transforms)`);
    const divider = await backendJob({ project: examples.find((example) => example.id === 'voltage_divider').project, operation: 'run', settings: { kind: 'sweep', source: 'V1', start: 0, stop: 10, step: 1 } });
    assert(divider.ok && divider.results[0].axis.length === 11, 'sweep samples');
    results.push('PASS real backend voltage-source sweep: 11 samples');
    const boundary = await backendJob({ project, operation: 'run', settings: { ...settings, step: .0001, stop: .2 } });
    assert(boundary.ok && boundary.results[0].axis.length === 2000, '2000-sample transient boundary');
    results.push('PASS real backend transient: exactly 2000 samples accepted');
    const overflow = await backendJob({ project, operation: 'run', settings: { ...settings, step: .0001, stop: .2001 } });
    assert(!overflow.ok && overflow.diagnostics.some((item) => /sample|limit/i.test(item.message)), '2001-sample transient must be rejected');
    results.push('PASS real backend transient: 2001 samples rejected');
    const draftKey = 'microspice.schematic-draft.v1', previousDraft = localStorage.getItem(draftKey);
    const scriptKey = 'microspice.script-draft.v1', previousScript = localStorage.getItem(scriptKey);
    const frame = document.createElement('iframe'); frame.title = 'Live workspace regression fixture';
    try {
      const saved = dividerProject();
      localStorage.setItem(draftKey, JSON.stringify({ project: saved, baseline: JSON.stringify(saved), filename: 'fixture.microspice', undo: [], redo: [] }));
      frame.srcdoc = await (await fetch('/')).text(); document.body.append(frame);
      await new Promise((resolve, reject) => { frame.onload = resolve; frame.onerror = reject; });
      const documentUnderTest = frame.contentDocument;
      const control = (id) => documentUnderTest.getElementById(id);
      const waitUntil = async (predicate, message) => {
        for (let attempt = 0; attempt < 200; attempt += 1) {
          if (predicate()) return;
          await new Promise((resolve) => setTimeout(resolve, 50));
        }
        throw new Error(message);
      };
      for (let attempt = 0; control('run').disabled && attempt < 100; attempt += 1) await new Promise((resolve) => setTimeout(resolve, 50));
      assert(!control('run').disabled, 'workspace engine unavailable');
      const changeSetting = (id, value) => { control(id).value = value; control(id).dispatchEvent(new Event('input', { bubbles: true })); };
      changeSetting('project-kind', 'transient'); changeSetting('project-step', '100u'); changeSetting('project-stop', '200m');
      control('run').click();
      await waitUntil(() => control('job-state').textContent === 'Idle', '2000-sample UI run timed out');
      assert(control('status').textContent === 'Run complete.' && /2000 samples/.test(control('results-content').textContent), `2000-sample UI: ${control('status').textContent}`);
      results.push('PASS live frontend transient: 2000 samples accepted and rendered');
      changeSetting('project-stop', '200.1m'); control('run').click();
      assert(/2,000 samples/.test(control('settings-error').textContent) && control('job-state').textContent === 'Idle', 'UI did not reject 2001 samples');
      results.push('PASS live frontend transient: 2001 samples rejected before submission');
      changeSetting('project-kind', 'dc');
      const liveWindow = frame.contentWindow, originalFetch = liveWindow.fetch;
      try {
        for (const action of ['edit', 'mode', 'stop']) {
          let releasePoll, pollReached = false;
          const released = new Promise((resolve) => { releasePoll = resolve; });
          liveWindow.fetch = async (url, options) => {
            const response = await originalFetch.call(liveWindow, url, options);
            if (/\/api\/jobs\/[^/]+$/.test(String(url)) && (!options?.method || options.method === 'GET')) {
              const payload = await response.clone().json();
              if (payload.state === 'done') { pollReached = true; await released; }
            }
            return response;
          };
          control('run').click();
          await waitUntil(() => pollReached || control('job-state').textContent === 'Idle', 'delayed real job did not reach completion');
          try {
            assert(pollReached, 'real job ended before delayed completion was captured');
            if (action === 'edit') changeSetting('project-step', '101u');
            else if (action === 'mode') control('mode-script').click();
            else control('stop').click();
            const unchanged = control('results-content').textContent;
            releasePoll();
            await new Promise((resolve) => setTimeout(resolve, 100));
            assert(control('results-content').textContent === unchanged && !/Run complete/.test(control('status').textContent) && control('job-state').textContent === 'Idle', `stale result published after ${action}`);
          } finally { releasePoll(); liveWindow.fetch = originalFetch; }
          if (action === 'mode') control('mode-schematic').click();
        }
      } finally { liveWindow.fetch = originalFetch; }
      results.push('PASS live frontend stale real-job completion discarded after settings edit, mode switch and Stop');
      documentUnderTest.querySelector('button[data-tool="wire"]').click();
      const canvas = documentUnderTest.querySelector('#schematic-canvas svg');
      for (const [pointX, pointY] of [[100, 40], [180, 80]]) {
        const point = new DOMPoint(pointX, pointY).matrixTransform(canvas.getScreenCTM());
        const target = canvas.querySelector('rect');
        target.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true, button: 0, clientX: point.x, clientY: point.y }));
        target.dispatchEvent(new PointerEvent('pointerup', { bubbles: true, button: 0, clientX: point.x, clientY: point.y }));
        target.dispatchEvent(new MouseEvent('click', { bubbles: true }));
      }
      assert(JSON.parse(localStorage.getItem(draftKey)).project.wires.length === 7, 'restored workspace lost wire start between clicks');
      canvas.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true }));
      assert(canvas.dataset.tool === 'select', 'Escape did not restore selection tool');
      control('project-undo').click();
      assert(JSON.parse(localStorage.getItem(draftKey)).project.wires.length === 5, 'wire undo');
      control('project-redo').click();
      assert(JSON.parse(localStorage.getItem(draftKey)).project.wires.length === 7, 'wire redo');
      const schematic = localStorage.getItem(draftKey);
      control('mode-script').click();
      control('source').value = '# independent script draft\n'; control('source').dispatchEvent(new Event('input', { bubbles: true }));
      control('mode-schematic').click();
      assert(localStorage.getItem(draftKey) === schematic && control('project-modified').textContent === 'Modified', 'switch changed schematic or saved state');
      control('mode-script').click();
      assert(control('source').value === '# independent script draft\n' && control('modified').textContent === 'Modified', 'switch changed script or saved state');
      results.push('PASS restored live workspace: wire clicks, Escape, undo/redo and independent modified documents');
    } finally {
      frame.remove();
      if (previousDraft === null) localStorage.removeItem(draftKey); else localStorage.setItem(draftKey, previousDraft);
      if (previousScript === null) localStorage.removeItem(scriptKey); else localStorage.setItem(scriptKey, previousScript);
    }
  } catch (error) { results.push(`FAIL backend integration: ${error.message}`); }
}
report.textContent = results.join('\n'); document.title = results.some((result) => result.startsWith('FAIL')) ? 'FAIL: Schematic Tests' : 'PASS: Schematic Tests';