import { SchematicEditor, emptyProject, engineering, parseEngineering, validateProject } from './schematic.js';

const byId = (id) => document.getElementById(id);
const source = byId('source');
const encoder = new TextEncoder();
const draftKey = 'microspice.script-draft.v1';
const maxFileBytes = 1024 * 1024;
let filename = 'Untitled.msim';
let baseline = '';
let revision = 0;
let ready = false;
let activeJob = null;
let result = null;
let examples = [];
let examplesLoading = false;
let plotRange = null;
let cursorIndex = null;
let drag = null;
let fileRequest = 0;
let draftTimer;
let healthTimer;
let draftWarning = false;
let mode = 'schematic';
let editor;
let projectFilename = 'Voltage divider.microspice';
let projectBaseline = '';
let projectExamples = [];
let projectLoad = 0;
let loadingProject = null;
const projectDraftKey = 'microspice.schematic-draft.v1';

function projectModified() { return editor.snapshot() !== projectBaseline; }
function updateProject() {
  byId('project-filename').textContent = projectFilename;
  byId('project-modified').textContent = projectModified() ? 'Modified' : 'Saved';
  byId('project-modified').classList.toggle('dirty', projectModified());
  byId('project-count').textContent = `${editor.project.components.length} components / ${editor.project.wires.length} wires`;
  byId('project-undo').disabled = !editor.undoStack.length;
  byId('project-redo').disabled = !editor.redoStack.length;
  byId('project-delete').disabled = !editor.selection;
  byId('project-rotate').disabled = !editor.selectedComponent();
  if (mode === 'schematic') document.title = `${projectModified() ? '* ' : ''}${projectFilename} - microspice`;
  updateControls();
}
function saveProjectDraft() {
  try { localStorage.setItem(projectDraftKey, JSON.stringify({ project: editor.project, baseline: projectBaseline, filename: projectFilename, undo: editor.undoStack, redo: editor.redoStack })); }
  catch { status('Schematic draft could not be saved. Download to keep your changes.'); }
}
function cancelProjectLoad() {
  projectLoad += 1;
  if (loadingProject) {
    loadingProject.controller.abort();
    if (loadingProject.id != null) void cancelRemote(loadingProject.id);
    loadingProject = null;
  }
}
function projectChanged() {
  revision += 1;
  cancelProjectLoad();
  cancelJob('Schematic changed; job discarded.');
  resetOutputs();
  updateProject();
  refreshSettings();
  saveProjectDraft();
  status('Schematic changed. Results cleared.');
}
function setMode(next) {
  if (next === mode) return;
  editor.endGesture(); editor.setTool('select'); updateTools();
  cancelJob('Workspace changed; job discarded.'); cancelProjectLoad();
  revision += 1; fileRequest += 1; mode = next;
  for (const name of ['schematic', 'script']) {
    byId(`${name === 'script' ? 'script' : 'schematic'}-pane`).hidden = name !== mode;
    byId(`mode-${name}`).setAttribute('aria-selected', String(name === mode));
    byId(`mode-${name}`).tabIndex = name === mode ? 0 : -1;
  }
  byId('analysis').hidden = mode === 'schematic';
  resetOutputs(); updateDocument(); updateProject();
  status(`${mode === 'schematic' ? 'Schematic' : 'Script'} workspace.`);
}
function updateTools() {
  document.querySelectorAll('button[data-tool]').forEach((button) => button.setAttribute('aria-pressed', String(button.dataset.tool === String(editor.tool))));
}
function selectProperties(component) {
  byId('component-properties').hidden = !component || component.kind === 5;
  byId('property-error').textContent = '';
  for (const id of ['property-value', 'property-ideality']) byId(id).removeAttribute('aria-invalid');
  if (component) {
    byId('property-reference').textContent = component.reference;
    byId('property-value').value = engineering(component.value);
    byId('property-unit').textContent = ['Ω', 'F', 'A', 'V', 'A', ''][component.kind];
    byId('property-value-label').textContent = component.kind === 2 ? 'Saturation' : 'Value';
    byId('property-ideality').value = String(component.ideality);
    byId('property-ideality').hidden = component.kind !== 2;
    byId('property-ideality-label').hidden = component.kind !== 2;
  }
  updateProject();
}
function refreshSettings() {
  const kind = byId('project-kind').value;
  document.querySelectorAll('[data-setting]').forEach((node) => { node.hidden = node.dataset.setting === 'sweep' ? kind !== 'sweep' : kind === 'dc'; });
  byId('stop-unit').textContent = byId('step-unit').textContent = kind === 'sweep' ? '(V)' : '(s)';
  const sourceName = byId('project-source').value;
  byId('project-source').replaceChildren();
  for (const component of editor.project.components.filter((component) => component.kind === 3)) byId('project-source').add(new Option(component.reference, component.reference));
  if ([...byId('project-source').options].some((option) => option.value === sourceName)) byId('project-source').value = sourceName;
  const voltages = new Map([...byId('initial-voltages').querySelectorAll('input')].map((input) => [input.dataset.reference, input.value]));
  byId('initial-voltages').replaceChildren();
  for (const component of editor.project.components.filter((component) => component.kind === 1)) {
    const label = element('label', `${component.reference} initial (V)`);
    const input = element('input'); input.dataset.reference = component.reference; input.id = `initial-${component.reference}`; input.value = voltages.get(component.reference) ?? '0'; label.htmlFor = input.id;
    byId('initial-voltages').append(label, input);
  }
  byId('initial-voltages').hidden = kind !== 'transient' || !byId('initial-voltages').children.length;
}
function analysisSettings() {
  const kind = byId('project-kind').value;
  const settings = { kind, initialVoltages: {} };
  if (kind === 'dc') return settings;
  const value = (id) => {
    const input = byId(id);
    try { const parsed = parseEngineering(input.value, kind === 'sweep' ? 'V' : 's'); input.removeAttribute('aria-invalid'); return parsed; }
    catch (error) { input.setAttribute('aria-invalid', 'true'); throw error; }
  };
  settings.step = value('project-step'); settings.stop = value('project-stop');
  settings.start = kind === 'sweep' ? value('project-start') : 0;
  const intervals = (settings.stop - settings.start) / settings.step;
  const limit = kind === 'sweep' ? 1999 : 2000;
  if (settings.step <= 0 || settings.stop <= settings.start || !Number.isFinite(intervals) || intervals > limit + Number.EPSILON * limit * 4) throw new Error('Step must be positive; stop must exceed start. Limit: 2,000 samples.');
  if (kind === 'sweep') {
    settings.source = byId('project-source').value;
    if (!settings.source) throw new Error('A voltage source is required for a sweep.');
  } else {
    for (const input of byId('initial-voltages').querySelectorAll('input')) {
      try { settings.initialVoltages[input.dataset.reference] = parseEngineering(input.value, 'V'); input.removeAttribute('aria-invalid'); }
      catch (error) { input.setAttribute('aria-invalid', 'true'); throw error; }
    }
  }
  return settings;
}
function replaceProject(project, name) {
  cancelProjectLoad(); cancelJob('Project replaced; job discarded.'); revision += 1;
  editor.replace(project); projectFilename = name; projectBaseline = editor.snapshot();
  byId('initial-voltages').replaceChildren();
  refreshSettings(); selectProperties(null); resetOutputs(); updateProject(); saveProjectDraft();
  status(`Opened ${name}`);
}
function canReplaceProject() { return !projectModified() || confirm('Discard the modified schematic and replace it? Download first to keep a copy.'); }
async function validateAndReplace(project, name, ticket) {
  validateProject(project);
  if (ticket !== projectLoad) return;
  const job = { id: null, controller: new AbortController() }; loadingProject = job;
  try {
    status(`Validating ${name}...`);
    const created = await request('/api/jobs', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ project, operation: 'validate_project' }), signal: job.controller.signal });
    if (created.id == null) throw new Error('Server did not return a validation job ID.');
    job.id = created.id;
    if (ticket !== projectLoad) { void cancelRemote(job.id); return; }
    for (;;) {
      const response = await request(`/api/jobs/${encodeURIComponent(job.id)}`, { signal: job.controller.signal });
      if (ticket !== projectLoad) return;
      if (response.state === 'done') {
        if (!response.result?.ok) throw new Error((response.result?.diagnostics || []).map((diagnostic) => diagnostic.message).join(' ') || 'Project validation failed.');
        const normalized = validateProject(response.result.normalizedProject ?? project);
        loadingProject = null;
        if (canReplaceProject() && ticket === projectLoad) replaceProject(normalized, name);
        else status('Project replacement cancelled.');
        return;
      }
      if (response.state !== 'running') throw new Error('Project validation was cancelled.');
      await pause(200, job.controller.signal);
    }
  } finally {
    if (loadingProject === job) { loadingProject = null; if (job.id != null) void cancelRemote(job.id); }
  }
}
async function openProject(file) {
  if (!file) return;
  editor.endGesture(); cancelProjectLoad(); const ticket = projectLoad;
  try {
    if (!/\.microspice$/i.test(file.name)) throw new Error('Choose a .microspice project.');
    if (file.size > 16 * 1024 * 1024) throw new Error('Project exceeds the 16 MiB file limit.');
    const text = new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer());
    if (ticket !== projectLoad) return;
    await validateAndReplace(JSON.parse(text), file.name, ticket);
  } catch (error) { if (ticket === projectLoad) status(`Project not opened: ${error instanceof TypeError ? 'File is not valid UTF-8.' : error.message}`); }
}
function saveProject() {
  editor.endGesture();
  download(JSON.stringify(editor.project, null, 2) + '\n', projectFilename, 'application/json;charset=utf-8');
  projectBaseline = editor.snapshot(); updateProject(); saveProjectDraft(); status(`Downloaded ${projectFilename}`);
}
function chooseFile() {
  if (mode === 'schematic') { editor.endGesture(); cancelProjectLoad(); }
  else fileRequest += 1;
  byId(mode === 'schematic' ? 'project-file' : 'file-input').click();
}
async function loadProjectExamples() {
  try {
    const response = await request('/api/project-examples');
    if (!Array.isArray(response)) throw new Error('Invalid project examples response.');
    projectExamples = response.filter((item) => typeof item.title === 'string' && item.id != null && item.project);
    byId('project-examples').replaceChildren(new Option('Examples', ''));
    for (const [index, example] of projectExamples.entries()) byId('project-examples').add(new Option(example.title, String(index)));
    byId('project-examples').disabled = !projectExamples.length;
  } catch { status('Project examples unavailable. The local divider, New, and Open remain available.'); }
}

function element(tag, text, className) {
  const node = document.createElement(tag);
  if (text !== undefined) node.textContent = text;
  if (className) node.className = className;
  return node;
}

function status(message) { byId('status').textContent = message; }
function modified() { return source.value !== baseline; }
function number(value) {
  if (!Number.isFinite(value)) return String(value ?? '');
  return value === 0 ? '0' : Number(value.toPrecision(7)).toString();
}
function quantity(value, unit) { return `${number(value)}${unit && unit !== '1' ? ` ${unit}` : ''}`; }

function tickNumber(value) {
  const magnitude = Math.abs(value);
  return magnitude !== 0 && (magnitude < 0.01 || magnitude >= 10000) ? value.toExponential(2) : Number(value.toPrecision(4)).toString();
}

function updateControls() {
  const busy = Boolean(activeJob);
  const empty = mode === 'script' ? !source.value.trim() : !editor?.project.components.length;
  byId('compile').disabled = busy || !ready || empty;
  byId('run').disabled = busy || !ready || empty;
  byId('stop').disabled = !busy;
  byId('analysis').disabled = busy || !byId('analysis').options.length || byId('analysis').options.length === 1;
  byId('job-state').textContent = busy ? (activeJob.operation === 'compile' ? 'Compiling' : 'Running') : 'Idle';
}

function updateDocument() {
  byId('filename').textContent = filename;
  byId('modified').textContent = modified() ? 'Modified' : 'Saved';
  byId('modified').classList.toggle('dirty', modified());
  if (mode === 'script') document.title = `${modified() ? '* ' : ''}${filename} - microspice`;
  byId('gutter').textContent = Array.from({ length: source.value.split('\n').length }, (_, index) => index + 1).join('\n');
  byId('gutter').scrollTop = source.scrollTop;
  updatePosition();
  updateControls();
}

function updatePosition() {
  const before = source.value.slice(0, source.selectionStart);
  const lines = before.split('\n');
  byId('position').textContent = `Ln ${lines.length}, Col ${lines.at(-1).length + 1}`;
}

function saveDraft() {
  clearTimeout(draftTimer);
  try {
    localStorage.setItem(draftKey, JSON.stringify({ source: source.value, baseline, filename }));
    draftWarning = false;
  } catch {
    draftWarning = true;
    status('Draft could not be saved in this browser. Download to keep your changes.');
  }
}

function resetOutputs() {
  result = null;
  plotRange = null;
  cursorIndex = null;
  byId('settings-error').textContent = '';
  byId('project-settings').querySelectorAll('[aria-invalid]').forEach((input) => input.removeAttribute('aria-invalid'));
  byId('analysis').replaceChildren(new Option('All analyses', ''));
  byId('summary').textContent = 'No compiled source';
  renderDiagnostics([]);
  renderResults();
  populatePlots();
}

async function request(url, options = {}, parseJson = true) {
  const controller = new AbortController();
  const relayAbort = () => controller.abort();
  const parent = options.signal;
  if (parent?.aborted) controller.abort();
  else parent?.addEventListener('abort', relayAbort, { once: true });
  const timer = setTimeout(() => controller.abort(new Error('Server request timed out')), 15000);
  try {
    const response = await fetch(url, { ...options, signal: controller.signal, cache: 'no-store' });
    if (!response.ok) {
      const failure = await response.json().catch(() => null);
      throw new Error(failure?.error || `Server returned HTTP ${response.status}`);
    }
    return parseJson ? await response.json() : undefined;
  } finally {
    clearTimeout(timer);
    parent?.removeEventListener('abort', relayAbort);
  }
}

async function cancelRemote(id, cancelledRevision = revision) {
  try {
    await request(`/api/jobs/${encodeURIComponent(id)}/cancel`, { method: 'POST' }, false);
  } catch {
    if (revision === cancelledRevision && !activeJob) status('Job discarded locally; server cancellation could not be confirmed.');
  }
}

function cancelJob(message) {
  const previous = activeJob;
  if (!previous) return;
  activeJob = null;
  previous.cancelled = true;
  previous.controller.abort();
  if (previous.id !== null) void cancelRemote(previous.id, previous.revision);
  updateControls();
  status(message);
}

function sourceChanged() {
  revision += 1;
  fileRequest += 1;
  cancelJob('Source changed; job discarded.');
  resetOutputs();
  updateDocument();
  clearTimeout(draftTimer);
  draftTimer = setTimeout(saveDraft, 300);
  status('Source changed. Results cleared.');
}

function canReplace() {
  return !modified() || confirm('Discard the modified script and replace it? Download first to keep a copy.');
}

function replaceSource(text, name) {
  cancelJob('Script replaced; job discarded.');
  revision += 1;
  fileRequest += 1;
  source.value = text;
  filename = name;
  baseline = text;
  source.scrollTop = 0;
  source.scrollLeft = 0;
  resetOutputs();
  updateDocument();
  saveDraft();
  source.focus();
  if (!draftWarning) status(`Opened ${filename}`);
}

function download(text, name, mime) {
  const url = URL.createObjectURL(new Blob([text], { type: mime }));
  const anchor = element('a');
  anchor.href = url;
  anchor.download = name.replace(/[\/\\\x00-\x1f]/g, '_');
  document.body.append(anchor);
  anchor.click();
  anchor.remove();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function saveSource() {
  download(source.value, filename, 'text/plain;charset=utf-8');
  baseline = source.value;
  updateDocument();
  saveDraft();
  if (!draftWarning) status(`Downloaded ${filename}`);
}

async function openFile(file) {
  if (!file) return;
  const ticket = ++fileRequest;
  try {
    if (!/\.msim$/i.test(file.name)) throw new Error('Choose a .msim script file.');
    if (file.size > maxFileBytes) throw new Error('Script exceeds the 1 MiB file limit.');
    const bytes = await file.arrayBuffer();
    const text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    if (ticket !== fileRequest) return;
    if (canReplace()) replaceSource(text, file.name);
  } catch (error) {
    if (ticket === fileRequest) status(error instanceof TypeError ? 'File is not valid UTF-8.' : error.message);
  }
}

function pause(milliseconds, signal) {
  return new Promise((resolve, reject) => {
    const onAbort = () => { clearTimeout(timer); reject(new DOMException('Aborted', 'AbortError')); };
    const timer = setTimeout(() => { signal.removeEventListener('abort', onAbort); resolve(); }, milliseconds);
    if (signal.aborted) onAbort();
    else signal.addEventListener('abort', onAbort, { once: true });
  });
}

async function startJob(operation) {
  editor.endGesture();
  if (activeJob || !ready || (mode === 'script' && !source.value.trim())) return;
  let body;
  try {
    if (mode === 'schematic') {
      byId('settings-error').textContent = '';
      if (editor.project.components.length > 1000 || editor.project.wires.length > 2000) throw new Error('Simulation limit: 1,000 components and 2,000 wires.');
      if (!editor.project.components.some((component) => component.kind === 5)) throw new Error('Add a ground before compiling or running.');
      body = { project: structuredClone(editor.project), operation, settings: operation === 'run' ? analysisSettings() : { kind: 'dc' } };
    } else body = { source: source.value, operation, analysis: operation === 'run' ? byId('analysis').value : '' };
  } catch (error) { byId('settings-error').textContent = error.message; status(error.message); return; }
  const job = { revision, mode, operation, controller: new AbortController(), id: null, cancelled: false };
  activeJob = job;
  updateControls();
  status(operation === 'compile' ? 'Compiling source...' : 'Running simulation...');
  try {
    const created = await request('/api/jobs', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    });
    if (created.id === undefined || created.id === null) throw new Error('Server did not return a job ID.');
    job.id = created.id;
    if (job.cancelled || activeJob !== job || revision !== job.revision) {
      void cancelRemote(job.id, job.revision);
      return;
    }
    for (;;) {
      const response = await request(`/api/jobs/${encodeURIComponent(job.id)}`, { signal: job.controller.signal });
      if (activeJob !== job || revision !== job.revision) return;
      if (response.state === 'cancelled') { status('Job cancelled.'); break; }
      if (response.state === 'done') {
        if (!response.result || typeof response.result.ok !== 'boolean') throw new Error('Server returned an invalid result.');
        applyResult(response.result, operation);
        break;
      }
      if (response.state !== 'running') throw new Error('Server returned an unknown job state.');
      await pause(350, job.controller.signal);
    }
  } catch (error) {
    if (activeJob === job && revision === job.revision) {
      if (job.id !== null) void cancelRemote(job.id, job.revision);
      resetOutputs();
      status(`Job failed: ${error.message || 'Connection interrupted'}`);
      void checkHealth();
    }
  } finally {
    if (activeJob === job) { activeJob = null; updateControls(); }
  }
}

function applyResult(payload, operation) {
  result = payload;
  renderDiagnostics(payload.diagnostics || []);
  const previous = byId('analysis').value;
  byId('analysis').replaceChildren(new Option('All analyses', ''));
  for (const analysis of payload.analyses || []) byId('analysis').add(new Option(`${analysis.name} (${analysis.kind})`, analysis.name));
  if ([...byId('analysis').options].some((option) => option.value === previous)) byId('analysis').value = previous;
  byId('summary').textContent = `${payload.components ?? 0} components / ${payload.nodes?.length ?? 0} nodes / ${payload.analyses?.length ?? 0} analyses`;
  renderResults();
  populatePlots();
  status(!payload.ok ? 'Simulation reported errors.' : operation === 'compile' ? 'Compilation complete.' : payload.checksPassed === false ? 'Run complete. Checks failed.' : 'Run complete.');
}

function byteToIndex(byteOffset) {
  const bytes = encoder.encode(source.value);
  const offset = Math.max(0, Math.min(bytes.length, byteOffset));
  return new TextDecoder('utf-8').decode(bytes.slice(0, offset)).length;
}

function diagnosticOffset(diagnostic) {
  if (Number.isFinite(diagnostic.offset) && diagnostic.offset >= 0) return diagnostic.offset;
  const lines = source.value.split('\n');
  const line = Math.max(0, Math.min(lines.length - 1, (diagnostic.line || 1) - 1));
  const preceding = lines.slice(0, line).map((text) => `${text}\n`).join('');
  return encoder.encode(preceding).length + Math.min(encoder.encode(lines[line]).length, Math.max(0, (diagnostic.column || 1) - 1));
}

function focusDiagnostic(diagnostic) {
  if (mode !== 'script') return;
  const offset = diagnosticOffset(diagnostic);
  const start = byteToIndex(offset);
  const end = byteToIndex(offset + Math.max(1, diagnostic.length || 1));
  source.focus();
  source.setSelectionRange(start, Math.min(source.value.length, Math.max(start + 1, end)));
  const line = source.value.slice(0, start).split('\n').length - 1;
  const lineHeight = parseFloat(getComputedStyle(source).lineHeight);
  source.scrollTop = Math.max(0, line * lineHeight - source.clientHeight / 3);
  source.scrollLeft = 0;
  byId('gutter').scrollTop = source.scrollTop;
  updatePosition();
}

function renderDiagnostics(diagnostics) {
  const list = byId('diagnostics');
  list.replaceChildren();
  byId('diagnostic-count').textContent = diagnostics.length;
  for (const diagnostic of diagnostics) {
    const item = element('li');
    const button = element('button', `${diagnostic.warning ? 'Warning' : 'Error'}${diagnostic.line ? ` ${diagnostic.line}:${diagnostic.column || 1}` : ''}  ${diagnostic.message}`, diagnostic.warning ? 'warning' : 'error');
    button.title = mode === 'script' ? 'Select source location' : 'Schematic diagnostic';
    button.addEventListener('click', () => focusDiagnostic(diagnostic));
    item.append(button);
    list.append(item);
  }
  if (!diagnostics.length) list.append(element('li', 'No diagnostics', 'empty'));
}

function resultTable(title, headers, rows) {
  const section = element('section', undefined, 'result-section');
  section.append(element('h2', title));
  const wrapper = element('div', undefined, 'table-scroll');
  const table = element('table');
  const head = element('thead');
  const headRow = element('tr');
  for (const label of headers) { const cell = element('th', label); cell.scope = 'col'; headRow.append(cell); }
  head.append(headRow);
  const body = element('tbody');
  for (const values of rows) {
    const row = element('tr');
    for (const value of values) {
      const cell = element('td');
      if (value instanceof Node) cell.append(value); else cell.textContent = value;
      row.append(cell);
    }
    body.append(row);
  }
  table.append(head, body);
  wrapper.append(table);
  section.append(wrapper);
  return section;
}

function renderResults() {
  const content = byId('results-content');
  content.replaceChildren();
  if (!result) { content.append(element('p', 'No simulation results', 'empty')); return; }
  content.append(element('div', result.ok ? 'VALID' : 'ERROR', `result-state ${result.ok ? 'pass' : 'fail'}`));
  for (const analysis of result.results || []) {
    const scalarRows = (analysis.observations || []).filter((trace) => trace.scalar || analysis.kind === 'dc').map((trace) => [trace.label, quantity(trace.data?.[0], trace.unit)]);
    if (scalarRows.length) content.append(resultTable(`${analysis.name} / ${analysis.kind.toUpperCase()}`, ['Observation', 'Value'], scalarRows));
    else content.append(element('p', `${analysis.name} / ${analysis.kind.toUpperCase()} / ${analysis.axis?.length ?? 0} samples`, 'analysis-summary'));
  }
  if (result.measurements?.length) content.append(resultTable('Measurements', ['Name', 'Value'], result.measurements.map((item) => [item.name, quantity(item.value, item.unit)])));
  const checks = (result.checks || []).map((item) => [item.name, element('span', item.passed ? 'PASS' : 'FAIL', `badge ${item.passed ? 'pass' : 'fail'}`)]);
  for (const skipped of result.skipped || []) checks.push([skipped, element('span', 'SKIP', 'badge skip')]);
  if (checks.length) content.append(resultTable('Checks', ['Name', 'Status'], checks));
  if (!(result.results?.length || result.measurements?.length || checks.length)) content.append(element('p', result.ok ? 'Source compiled. No executed analyses.' : 'No results available.', 'empty'));
}

function selectedAnalysis() { return result?.results?.[Number(byId('plot-analysis').value)]; }
function selectedTrace() { return selectedAnalysis()?.observations?.[Number(byId('trace').value)]; }
function points() {
  const analysis = selectedAnalysis();
  const trace = selectedTrace();
  if (!analysis || !trace) return [];
  return (trace.data || []).map((value, index) => ({ x: analysis.axis?.[index] ?? (trace.scalar ? 0 : index), y: value, index })).filter((point) => Number.isFinite(point.x) && Number.isFinite(point.y));
}

function populatePlots() {
  const select = byId('plot-analysis');
  select.replaceChildren();
  for (const [index, analysis] of (result?.results || []).entries()) select.add(new Option(`${analysis.name} (${analysis.kind})`, String(index)));
  if (!select.options.length) select.add(new Option('No results', ''));
  select.disabled = !result?.results?.length;
  populateTraces();
}

function populateTraces() {
  const select = byId('trace');
  select.replaceChildren();
  for (const [index, trace] of (selectedAnalysis()?.observations || []).entries()) select.add(new Option(`${trace.label}${trace.unit && trace.unit !== '1' ? ` (${trace.unit})` : ''}`, String(index)));
  if (!select.options.length) select.add(new Option('No traces', ''));
  select.disabled = !selectedAnalysis()?.observations?.length;
  fitPlot();
}

function fitPlot() {
  plotRange = null;
  cursorIndex = null;
  updatePlotControls();
  renderSamples();
  drawPlot();
}

function updatePlotControls() {
  const available = points().length > 0;
  for (const id of ['fit', 'zoom-in', 'zoom-out']) byId(id).disabled = !available;
  byId('csv').disabled = !selectedAnalysis()?.csv;
  byId('cursor').textContent = available ? `${points().length} samples` : 'No trace selected';
  byId('cursor').title = byId('cursor').textContent;
}

function axisLabel() {
  const analysis = selectedAnalysis();
  return analysis?.kind === 'transient' ? 'Time (s)' : analysis?.kind === 'sweep' ? `Sweep${analysis.axisUnit && analysis.axisUnit !== '1' ? ` (${analysis.axisUnit})` : ''}` : 'Sample';
}

function renderSamples() {
  const analysis = selectedAnalysis();
  const trace = selectedTrace();
  byId('samples-title').textContent = trace ? `${analysis.name} / ${trace.label}` : 'Samples';
  const data = trace?.data || [];
  const count = Math.min(data.length, 2000);
  byId('samples-count').textContent = `${count} rows${data.length > count ? ` of ${data.length}` : ''}`;
  const headings = byId('samples').querySelectorAll('th');
  headings[1].textContent = axisLabel();
  headings[2].textContent = trace ? `${trace.label}${trace.unit && trace.unit !== '1' ? ` (${trace.unit})` : ''}` : 'Value';
  const body = byId('samples').querySelector('tbody');
  const fragment = document.createDocumentFragment();
  for (let index = 0; index < count; index += 1) {
    const row = element('tr');
    row.append(element('td', index + 1), element('td', number(analysis.axis?.[index] ?? (trace.scalar ? 0 : index))), element('td', number(data[index])));
    fragment.append(row);
  }
  body.replaceChildren(fragment);
}

function bounds(data) {
  let xmin = Infinity, xmax = -Infinity, ymin = Infinity, ymax = -Infinity;
  for (const point of data) { xmin = Math.min(xmin, point.x); xmax = Math.max(xmax, point.x); ymin = Math.min(ymin, point.y); ymax = Math.max(ymax, point.y); }
  if (xmin === xmax) { xmin -= Math.max(Math.abs(xmin) * 0.05, 0.5); xmax += Math.max(Math.abs(xmax) * 0.05, 0.5); }
  const padding = ymax === ymin ? Math.max(Math.abs(ymax) * 0.05, 0.5) : (ymax - ymin) * 0.08;
  return { xmin, xmax, ymin: ymin - padding, ymax: ymax + padding };
}

function plotGeometry() {
  const canvas = byId('plot');
  const width = canvas.clientWidth;
  const height = canvas.clientHeight;
  return { width, height, left: width < 400 ? 68 : 78, right: width - 28, top: 36, bottom: height - 48 };
}

function drawPlot() {
  const canvas = byId('plot');
  const geometry = plotGeometry();
  if (!geometry.width || !geometry.height) return;
  const ratio = window.devicePixelRatio || 1;
  canvas.width = Math.round(geometry.width * ratio);
  canvas.height = Math.round(geometry.height * ratio);
  const context = canvas.getContext('2d');
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.fillStyle = '#ffffff';
  context.fillRect(0, 0, geometry.width, geometry.height);
  context.font = '11px Courier, monospace';
  const data = points();
  if (!data.length) {
    context.fillStyle = '#69757b'; context.textAlign = 'center';
    context.fillText('No waveform data', geometry.width / 2, geometry.height / 2);
    byId('plot-caption').textContent = 'No waveform data';
    canvas.setAttribute('aria-label', 'Waveform plot. No waveform data.');
    return;
  }
  const range = plotRange || bounds(data);
  const xpixel = (value) => geometry.left + (value - range.xmin) / (range.xmax - range.xmin) * (geometry.right - geometry.left);
  const ypixel = (value) => geometry.bottom - (value - range.ymin) / (range.ymax - range.ymin) * (geometry.bottom - geometry.top);
  context.lineWidth = 1;
  for (let index = 0; index <= 4; index += 1) {
    const fraction = index / 4;
    const horizontal = geometry.left + fraction * (geometry.right - geometry.left);
    const vertical = geometry.bottom - fraction * (geometry.bottom - geometry.top);
    context.strokeStyle = '#e7ecef'; context.beginPath();
    context.moveTo(horizontal, geometry.top); context.lineTo(horizontal, geometry.bottom);
    context.moveTo(geometry.left, vertical); context.lineTo(geometry.right, vertical); context.stroke();
    context.fillStyle = '#56646b'; context.textAlign = 'center';
    context.fillText(tickNumber(range.xmin + fraction * (range.xmax - range.xmin)), horizontal, geometry.bottom + 18, (geometry.right - geometry.left) / 4 - 4);
    context.textAlign = 'right';
    context.fillText(tickNumber(range.ymin + fraction * (range.ymax - range.ymin)), geometry.left - 8, vertical + 4, geometry.left - 12);
  }
  context.strokeStyle = '#9daab1'; context.beginPath();
  context.moveTo(geometry.left, geometry.top); context.lineTo(geometry.left, geometry.bottom); context.lineTo(geometry.right, geometry.bottom); context.stroke();
  context.fillStyle = '#34464d'; context.textAlign = 'left';
  const trace = selectedTrace();
  context.fillText(`${trace.label}${trace.unit && trace.unit !== '1' ? ` (${trace.unit})` : ''}`, geometry.left, 18, geometry.right - geometry.left);
  context.textAlign = 'center'; context.fillText(axisLabel(), (geometry.left + geometry.right) / 2, geometry.height - 10);
  context.save(); context.beginPath(); context.rect(geometry.left, geometry.top, geometry.right - geometry.left, geometry.bottom - geometry.top); context.clip();
  context.strokeStyle = '#168361'; context.lineWidth = 2; context.beginPath();
  data.forEach((point, index) => { if (index === 0) context.moveTo(xpixel(point.x), ypixel(point.y)); else context.lineTo(xpixel(point.x), ypixel(point.y)); });
  context.stroke();
  if (data.length === 1) { context.fillStyle = '#168361'; context.beginPath(); context.arc(xpixel(data[0].x), ypixel(data[0].y), 4, 0, Math.PI * 2); context.fill(); }
  const cursor = data.find((point) => point.index === cursorIndex);
  if (cursor) {
    context.strokeStyle = '#cf6555'; context.lineWidth = 1; context.setLineDash([4, 4]); context.beginPath();
    context.moveTo(xpixel(cursor.x), geometry.top); context.lineTo(xpixel(cursor.x), geometry.bottom); context.stroke(); context.setLineDash([]);
    context.fillStyle = '#cf6555'; context.beginPath(); context.arc(xpixel(cursor.x), ypixel(cursor.y), 4, 0, Math.PI * 2); context.fill();
    byId('cursor').textContent = `${quantity(cursor.x, selectedAnalysis().axisUnit)} / ${quantity(cursor.y, trace.unit)}`;
    byId('cursor').title = byId('cursor').textContent;
  }
  context.restore();
  byId('plot-caption').textContent = `${selectedAnalysis().name} / ${trace.label} / ${data.length} samples`;
  canvas.setAttribute('aria-label', `${byId('plot-caption').textContent}. ${axisLabel()}. Value in ${trace.unit || 'dimensionless units'}.`);
}

function zoom(factor, fraction = 0.5) {
  const data = points();
  if (!data.length) return;
  const range = plotRange || bounds(data);
  const anchor = range.xmin + fraction * (range.xmax - range.xmin);
  const xmin = anchor + (range.xmin - anchor) * factor;
  const xmax = anchor + (range.xmax - anchor) * factor;
  if (!Number.isFinite(xmin) || !Number.isFinite(xmax) || xmax - xmin <= Number.EPSILON * Math.max(1, Math.abs(anchor))) return;
  plotRange = { ...range, xmin, xmax };
  drawPlot();
}

function canvasFraction(event) {
  const geometry = plotGeometry();
  const rect = byId('plot').getBoundingClientRect();
  return Math.max(0, Math.min(1, (event.clientX - rect.left - geometry.left) / (geometry.right - geometry.left)));
}

function inspectCursor(event) {
  const data = points();
  if (!data.length) return;
  const range = plotRange || bounds(data);
  const target = range.xmin + canvasFraction(event) * (range.xmax - range.xmin);
  let nearest = data[0];
  for (const point of data) if (Math.abs(point.x - target) < Math.abs(nearest.x - target)) nearest = point;
  cursorIndex = nearest.index;
  drawPlot();
}

function showTab(name) {
  for (const view of ['results', 'waveform', 'samples']) {
    const chosen = name === view;
    byId(`tab-${view}`).setAttribute('aria-selected', String(chosen));
    byId(`tab-${view}`).tabIndex = chosen ? 0 : -1;
    byId(`${view}-panel`).hidden = !chosen;
  }
  byId('trace-controls').hidden = name === 'results';
  if (name === 'waveform') drawPlot();
}

function showFind() { byId('find-form').hidden = false; byId('find-input').focus(); byId('find-input').select(); }
function findNext(event) {
  event?.preventDefault();
  const term = byId('find-input').value;
  if (!term) return;
  let index = source.value.indexOf(term, source.selectionEnd);
  const wrapped = index === -1;
  if (wrapped) index = source.value.indexOf(term);
  byId('find-status').textContent = index === -1 ? 'No match' : wrapped ? 'Wrapped' : 'Match';
  if (index !== -1) {
    source.focus(); source.setSelectionRange(index, index + term.length);
    const lineHeight = parseFloat(getComputedStyle(source).lineHeight);
    source.scrollTop = Math.max(0, (source.value.slice(0, index).split('\n').length - 1) * lineHeight - source.clientHeight / 3);
    byId('gutter').scrollTop = source.scrollTop;
    updatePosition();
  }
}

async function checkHealth() {
  clearTimeout(healthTimer);
  try { ready = (await request('/api/health')).ready === true; }
  catch { ready = false; }
  byId('health').textContent = ready ? 'Engine ready' : 'Engine unavailable';
  byId('health').classList.toggle('online', ready);
  updateControls();
  if (ready && !examples.length && !examplesLoading) void loadExamples();
  if (ready && !projectExamples.length) void loadProjectExamples();
  healthTimer = setTimeout(checkHealth, ready ? 30000 : 5000);
  return ready;
}

async function loadExamples() {
  if (examplesLoading) return;
  examplesLoading = true;
  try {
    const response = await request('/api/examples');
    if (!Array.isArray(response)) throw new Error('Invalid examples response');
    examples = response.filter((item) => typeof item.source === 'string' && typeof item.title === 'string' && item.id != null);
    byId('examples').replaceChildren(new Option('Examples', ''));
    for (const [index, example] of examples.entries()) byId('examples').add(new Option(example.title, String(index)));
    byId('examples').disabled = !examples.length;
  } catch {
    if (!activeJob) status('Examples unavailable. New and Open remain available.');
  } finally { examplesLoading = false; }
}

source.addEventListener('input', sourceChanged);
source.addEventListener('scroll', () => { byId('gutter').scrollTop = source.scrollTop; });
for (const name of ['click', 'keyup', 'select']) source.addEventListener(name, updatePosition);
byId('new').addEventListener('click', () => { if (canReplace()) replaceSource('', 'Untitled.msim'); });
byId('open').addEventListener('click', chooseFile);
byId('file-input').addEventListener('change', (event) => { void openFile(event.target.files[0]); event.target.value = ''; });
byId('save').addEventListener('click', saveSource);
byId('examples').addEventListener('change', (event) => {
  if (event.target.value === '') return;
  const example = examples[Number(event.target.value)];
  if (example && canReplace()) replaceSource(example.source, `${String(example.id).replace(/\.msim$/i, '').replace(/[^a-zA-Z0-9_-]/g, '_')}.msim`);
  event.target.value = '';
});
byId('compile').addEventListener('click', () => void startJob('compile'));
byId('run').addEventListener('click', () => void startJob('run'));
byId('stop').addEventListener('click', () => cancelJob('Job stopped.'));
byId('find-button').addEventListener('click', showFind);
byId('find-form').addEventListener('submit', findNext);
byId('find-close').addEventListener('click', () => { byId('find-form').hidden = true; source.focus(); });
byId('plot-analysis').addEventListener('change', populateTraces);
byId('trace').addEventListener('change', fitPlot);
byId('fit').addEventListener('click', fitPlot);
byId('zoom-in').addEventListener('click', () => zoom(0.5));
byId('zoom-out').addEventListener('click', () => zoom(2));
byId('csv').addEventListener('click', () => {
  const analysis = selectedAnalysis();
  const name = mode === 'schematic' ? projectFilename : filename;
  if (analysis?.csv) download(analysis.csv, `${name.replace(/\.(msim|microspice)$/i, '')}-${analysis.name}.csv`, 'text/csv;charset=utf-8');
});
for (const name of ['results', 'waveform', 'samples']) {
  const tab = byId(`tab-${name}`);
  tab.addEventListener('click', () => showTab(name));
  tab.addEventListener('keydown', (event) => {
    const names = ['results', 'waveform', 'samples'];
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key)) return;
    event.preventDefault();
    const index = event.key === 'Home' ? 0 : event.key === 'End' ? 2 : (names.indexOf(name) + (event.key === 'ArrowRight' ? 1 : 2)) % 3;
    showTab(names[index]); byId(`tab-${names[index]}`).focus();
  });
}
const canvas = byId('plot');
canvas.addEventListener('pointerdown', (event) => {
  if (event.button !== 0 || !points().length) return;
  const range = plotRange || bounds(points());
  drag = { start: event.clientX, range, moved: false };
  canvas.setPointerCapture(event.pointerId); inspectCursor(event);
});
canvas.addEventListener('pointermove', (event) => {
  if (drag) {
    const geometry = plotGeometry();
    const delta = (event.clientX - drag.start) / (geometry.right - geometry.left) * (drag.range.xmax - drag.range.xmin);
    drag.moved ||= Math.abs(event.clientX - drag.start) > 3;
    if (drag.moved) { plotRange = { ...drag.range, xmin: drag.range.xmin - delta, xmax: drag.range.xmax - delta }; drawPlot(); }
  } else inspectCursor(event);
});
canvas.addEventListener('pointerup', (event) => { if (drag && !drag.moved) inspectCursor(event); drag = null; if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId); });
canvas.addEventListener('pointercancel', () => { drag = null; });
canvas.addEventListener('lostpointercapture', () => { drag = null; });
canvas.addEventListener('wheel', (event) => { if (points().length) { event.preventDefault(); zoom(event.deltaY > 0 ? 1.2 : 1 / 1.2, canvasFraction(event)); } }, { passive: false });
canvas.addEventListener('keydown', (event) => {
  if (event.key === '+' || event.key === '=') { event.preventDefault(); zoom(0.5); }
  else if (event.key === '-') { event.preventDefault(); zoom(2); }
  else if (event.key === 'Home') { event.preventDefault(); fitPlot(); }
  else if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
    const data = points(); if (!data.length) return; event.preventDefault();
    const current = Math.max(0, data.findIndex((point) => point.index === cursorIndex));
    cursorIndex = data[Math.max(0, Math.min(data.length - 1, current + (event.key === 'ArrowRight' ? 1 : -1)))].index; drawPlot();
  }
});
new ResizeObserver(drawPlot).observe(canvas);
editor = new SchematicEditor(byId('schematic-canvas'), projectChanged, selectProperties, status);
projectBaseline = editor.snapshot();
try {
  const stored = localStorage.getItem(projectDraftKey);
  if (stored) {
    const draft = JSON.parse(stored);
    validateProject(draft.project);
    validateProject(JSON.parse(draft.baseline));
    if (typeof draft.filename !== 'string') throw new Error('Invalid draft filename.');
    const history = (entries) => {
      if (entries === undefined) return [];
      if (!Array.isArray(entries) || entries.length > 100) throw new Error('Invalid draft history.');
      for (const entry of entries) validateProject(JSON.parse(entry));
      return entries;
    };
    const undo = history(draft.undo), redo = history(draft.redo);
    editor.replace(draft.project); editor.undoStack = undo; editor.redoStack = redo; projectBaseline = draft.baseline; projectFilename = draft.filename;
  }
} catch { status('Schematic draft unavailable. Loaded the built-in divider.'); }
function schematicAction(action) {
  try { action(); updateProject(); }
  catch (error) { status(error.message); }
}
for (const name of ['schematic', 'script']) {
  byId(`mode-${name}`).addEventListener('click', () => setMode(name));
  byId(`mode-${name}`).addEventListener('keydown', (event) => {
    if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') {
      event.preventDefault(); setMode(mode === 'schematic' ? 'script' : 'schematic'); byId(`mode-${mode}`).focus();
    }
  });
}
document.querySelectorAll('button[data-tool]').forEach((button) => button.addEventListener('click', () => {
  editor.endGesture(); editor.setTool(/^\d$/.test(button.dataset.tool) ? Number(button.dataset.tool) : button.dataset.tool); updateTools();
}));
byId('project-undo').addEventListener('click', () => schematicAction(() => editor.history()));
byId('project-redo').addEventListener('click', () => schematicAction(() => editor.history(true)));
byId('project-delete').addEventListener('click', () => schematicAction(() => editor.remove()));
byId('project-rotate').addEventListener('click', () => schematicAction(() => editor.rotate()));
byId('project-fit').addEventListener('click', () => editor.fit());
byId('project-zoom-in').addEventListener('click', () => editor.zoom(1.25));
byId('project-zoom-out').addEventListener('click', () => editor.zoom(0.8));
byId('component-properties').addEventListener('submit', (event) => {
  event.preventDefault();
  const component = editor.selectedComponent(); if (!component) return;
  let field = byId('property-value');
  try {
    const value = parseEngineering(field.value, ['Ω', 'F', 'A', 'V', 'A', ''][component.kind]);
    if (component.kind < 3 && value <= 0) throw new Error('Value must be positive.');
    field.removeAttribute('aria-invalid');
    let ideality = component.ideality;
    if (component.kind === 2) {
      field = byId('property-ideality'); ideality = parseEngineering(field.value);
      if (ideality <= 0) throw new Error('Ideality must be positive.');
      field.removeAttribute('aria-invalid');
    }
    editor.edit(() => { component.value = value; component.ideality = ideality; });
    byId('property-error').textContent = '';
  } catch (error) { field.setAttribute('aria-invalid', 'true'); byId('property-error').textContent = error.message; }
});
byId('project-settings').addEventListener('input', (event) => {
  revision += 1; cancelProjectLoad(); cancelJob('Analysis settings changed; job discarded.'); resetOutputs();
  byId('settings-error').textContent = '';
  if (event.target.id === 'project-kind') {
    const sweep = event.target.value === 'sweep';
    byId('project-stop').value = sweep ? '10' : '10m'; byId('project-step').value = sweep ? '0.1' : '100u'; refreshSettings();
  }
  status('Analysis settings changed. Results cleared.');
});
byId('project-new').addEventListener('click', () => { editor.endGesture(); cancelProjectLoad(); if (canReplaceProject()) replaceProject(emptyProject(), 'Untitled.microspice'); });
byId('project-open').addEventListener('click', chooseFile);
byId('project-file').addEventListener('change', (event) => { void openProject(event.target.files[0]); event.target.value = ''; });
byId('project-save').addEventListener('click', saveProject);
byId('project-examples').addEventListener('change', async (event) => {
  const example = projectExamples[Number(event.target.value)];
  if (event.target.value === '' || !example) return;
  event.target.value = ''; editor.endGesture(); cancelProjectLoad(); const ticket = projectLoad;
  try { await validateAndReplace(example.project, `${String(example.id).replace(/\.microspice$/i, '').replace(/[^a-zA-Z0-9_-]/g, '_')}.microspice`, ticket); }
  catch (error) { if (ticket === projectLoad) status(`Project not opened: ${error.message}`); }
});
byId('schematic-canvas').addEventListener('pointerup', () => updateProject());
byId('schematic-canvas').addEventListener('keydown', (event) => {
  if (event.ctrlKey || event.metaKey || event.altKey) return;
  const deltas = { ArrowLeft: [-20, 0], ArrowRight: [20, 0], ArrowUp: [0, -20], ArrowDown: [0, 20] };
  if (event.key in deltas) { event.preventDefault(); schematicAction(() => editor.move(...deltas[event.key])); }
  else if (event.key === 'Delete' || event.key === 'Backspace') { event.preventDefault(); schematicAction(() => editor.remove()); }
  else if (event.key.toLowerCase() === 'r') { event.preventDefault(); schematicAction(() => editor.rotate()); }
  else if (event.key === 'Escape') { event.preventDefault(); editor.setTool('select'); editor.select(null); updateTools(); updateProject(); }
});
byId('help-button').addEventListener('click', () => byId('help-dialog').showModal());
byId('help-close').addEventListener('click', () => byId('help-dialog').close());
document.addEventListener('keydown', (event) => {
  if (byId('help-dialog').open) return;
  const command = event.ctrlKey || event.metaKey;
  if (command && event.key.toLowerCase() === 's') { event.preventDefault(); mode === 'schematic' ? saveProject() : saveSource(); }
  else if (command && event.key.toLowerCase() === 'o') { event.preventDefault(); chooseFile(); }
  else if (command && event.key.toLowerCase() === 'f') { event.preventDefault(); mode === 'schematic' ? editor.fit() : showFind(); }
  else if (command && mode === 'schematic' && !event.target.matches('input, textarea') && ['z', 'y'].includes(event.key.toLowerCase())) { event.preventDefault(); schematicAction(() => editor.history(event.shiftKey || event.key.toLowerCase() === 'y')); }
  else if (event.key === 'F5') { event.preventDefault(); void startJob('run'); }
  else if (event.key === 'Escape' && mode === 'schematic') { editor.setTool('select'); editor.select(null); updateTools(); updateProject(); }
  else if (event.key === 'Escape' && !byId('find-form').hidden) { byId('find-form').hidden = true; source.focus(); }
});
window.addEventListener('beforeunload', (event) => {
  editor.endGesture();
  if (modified() || projectModified()) { event.preventDefault(); event.returnValue = ''; }
});
window.addEventListener('pagehide', () => { saveDraft(); saveProjectDraft(); cancelProjectLoad(); if (activeJob?.id != null) void cancelRemote(activeJob.id); });
document.addEventListener('visibilitychange', () => { if (document.hidden) { saveDraft(); saveProjectDraft(); } });

try {
  const stored = localStorage.getItem(draftKey);
  if (stored) {
    const draft = JSON.parse(stored);
    if (typeof draft.source === 'string' && typeof draft.baseline === 'string' && typeof draft.filename === 'string') {
      source.value = draft.source; baseline = draft.baseline; filename = draft.filename;
      status('Restored local draft.');
    }
  }
} catch { status('Local draft unavailable.'); }
updateDocument();
updateProject(); refreshSettings(); editor.fit(); byId('analysis').hidden = true;
resetOutputs();
void checkHealth().then((connected) => { if (byId('status').textContent === 'Connecting to engine') status(connected ? 'Ready.' : 'Engine unavailable. Retrying connection.'); });
void loadExamples();
void loadProjectExamples();
