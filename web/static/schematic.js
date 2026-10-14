const NS = 'http://www.w3.org/2000/svg';
const prefixes = ['R', 'C', 'D', 'V', 'I', 'G'];
const units = ['Ω', 'F', 'A', 'V', 'A', ''];
const defaults = [1000, 1e-6, 1e-12, 5, 0.001, 0];
const clone = (value) => structuredClone(value);
const same = (first, second) => first[0] === second[0] && first[1] === second[1];
const snap = (value) => Math.round(value / 20) * 20;
export const emptyProject = () => ({ format: 'microspice.project', version: 1, components: [], wires: [], counters: [0, 0, 0, 0, 0, 0] });
export function dividerProject() {
  return { ...emptyProject(), counters: [2, 0, 0, 1, 0, 1], components: [
    { kind: 3, reference: 'V1', position: [0, 0], rotation: 0, value: 10, ideality: 1 },
    { kind: 0, reference: 'R1', position: [0, -120], rotation: 0, value: 1000, ideality: 1 },
    { kind: 0, reference: 'R2', position: [240, -120], rotation: 0, value: 1000, ideality: 1 },
    { kind: 5, reference: 'G1', position: [60, 120], rotation: 0, value: 0, ideality: 1 }
  ], wires: [[[-60, 0], [-60, -120]], [[60, -120], [180, -120]], [[300, -120], [300, 120]], [[300, 120], [60, 120]], [[60, 0], [60, 120]]].map(([start, end]) => ({ start, end })) };
}
export function validateProject(project) {
  const finite = (value) => typeof value === 'number' && Number.isFinite(value);
  const point = (value) => Array.isArray(value) && value.length === 2 && value.every((coordinate) => finite(coordinate) && Math.abs(coordinate) <= 1000000 && coordinate % 20 === 0);
  if (!project || project.format !== 'microspice.project' || project.version !== 1 || !Array.isArray(project.components) || !Array.isArray(project.wires) || !Array.isArray(project.counters) || project.counters.length !== 6 || project.components.length > 10000 || project.wires.length > 50000 || !project.counters.every((counter) => Number.isInteger(counter) && counter >= 0 && counter <= 999999999)) throw new Error('Invalid version-1 microspice project or project limits exceeded.');
  const references = new Set();
  for (const component of project.components) {
    if (!component || !Number.isInteger(component.kind) || component.kind < 0 || component.kind > 5 || typeof component.reference !== 'string' || !new RegExp(`^${prefixes[component.kind]}[1-9][0-9]{0,8}$`).test(component.reference) || references.has(component.reference) || !point(component.position) || ![0, 90, 180, 270].includes(component.rotation) || !finite(component.value) || (component.kind < 3 && component.value <= 0) || (component.kind === 5 && component.value !== 0) || !finite(component.ideality) || component.ideality <= 0) throw new Error('Invalid component, value, position, or duplicate reference.');
    references.add(component.reference);
  }
  for (const wire of project.wires) if (!wire || !point(wire.start) || !point(wire.end) || same(wire.start, wire.end) || (wire.start[0] !== wire.end[0] && wire.start[1] !== wire.end[1])) throw new Error('Wires must be nonzero orthogonal grid segments.');
  return project;
}
export function engineering(value) {
  if (value === 0) return '0';
  for (const [power, suffix] of [[12, 'T'], [9, 'G'], [6, 'M'], [3, 'k'], [0, ''], [-3, 'm'], [-6, 'u'], [-9, 'n'], [-12, 'p']]) {
    if (Math.abs(value) >= 10 ** power) return `${Number((value / 10 ** power).toPrecision(12))}${suffix}`;
  }
  return String(value);
}
export function parseEngineering(text, unit = '') {
  const normalized = text.trim().replace(/µ|μ/g, 'u');
  const match = normalized.match(/^([+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)\s*(meg|[TGMkKmunp]?)\s*(Ω|ohms?|[FVAs])?$/);
  if (!match || (match[3] && !(match[3] === unit || (unit === 'Ω' && /^ohms?$/i.test(match[3]))))) throw new Error('Enter a finite SI value with a matching unit.');
  const suffix = match[2];
  const factors = { T: 1e12, G: 1e9, M: 1e6, k: 1e3, K: 1e3, m: 1e-3, u: 1e-6, n: 1e-9, p: 1e-12, '': 1 };
  const value = Number(match[1]) * (suffix.toLowerCase() === 'meg' ? 1e6 : factors[suffix]);
  if (!Number.isFinite(value)) throw new Error('Value must be finite.');
  return value;
}
export function pins(component) {
  const [centerX, centerY] = component.position;
  if (component.kind === 5) return [[centerX, centerY]];
  const directions = { 0: [1, 0], 90: [0, 1], 180: [-1, 0], 270: [0, -1] };
  const [deltaX, deltaY] = directions[component.rotation];
  return [[centerX - deltaX * 60, centerY - deltaY * 60], [centerX + deltaX * 60, centerY + deltaY * 60]];
}
const onWire = (point, wire) => wire.start[0] === wire.end[0] ? point[0] === wire.start[0] && point[1] >= Math.min(wire.start[1], wire.end[1]) && point[1] <= Math.max(wire.start[1], wire.end[1]) : point[1] === wire.start[1] && point[0] >= Math.min(wire.start[0], wire.end[0]) && point[0] <= Math.max(wire.start[0], wire.end[0]);
export function route(start, end) {
  if (same(start, end)) return [];
  if (start[0] === end[0] || start[1] === end[1]) return [{ start: [...start], end: [...end] }];
  const elbow = [end[0], start[1]];
  return [{ start: [...start], end: elbow }, { start: elbow, end: [...end] }];
}
export function rerouteAttached(project, before, after) {
  const oldPins = pins(before);
  const newPins = pins(after);
  if (oldPins.every((point, index) => same(point, newPins[index]))) return;
  const originalWires = project.wires;
  const original = { ...project, components: project.components.map((component) => component.reference === before.reference ? before : component) };
  const signature = (candidate) => {
    const anchors = new Map(), parents = [];
    const anchor = (point) => {
      const key = point.join(',');
      if (!anchors.has(key)) { anchors.set(key, { point, index: parents.length }); parents.push(parents.length); }
      return anchors.get(key).index;
    };
    const root = (index) => { while (parents[index] !== index) { parents[index] = parents[parents[index]]; index = parents[index]; } return index; };
    const join = (first, second) => { parents[root(first)] = root(second); };
    const labels = candidate.components.flatMap((component) => pins(component).map(anchor));
    const grounds = candidate.components.filter((component) => component.kind === 5).map((component) => anchor(component.position));
    for (const ground of grounds) join(ground, grounds[0]);
    for (const wire of originalWires) labels.push(anchor(wire.start));
    for (const wire of candidate.wires) join(anchor(wire.start), anchor(wire.end));
    if (anchors.size * candidate.wires.length > 2000000) throw new Error('Move rejected: routing safety check exceeds the interactive limit.');
    for (const { point, index } of anchors.values()) for (const wire of candidate.wires) if (onWire(point, wire)) join(index, anchor(wire.start));
    const names = new Map();
    return labels.map((index) => { const net = root(index); if (!names.has(net)) names.set(net, names.size); return names.get(net); }).join(',');
  };
  const baseline = signature(original);
  const attached = oldPins.map((point) => originalWires.some((wire) => onWire(point, wire)) || original.components.some((component) => component.reference !== after.reference && pins(component).some((pin) => same(point, pin))));
  for (let choice = 0; choice < 2 ** oldPins.length; choice += 1) {
    const additions = oldPins.flatMap((point, index) => !attached[index] ? [] : choice & (1 << index) ? route(point, newPins[index]) : route(newPins[index], point));
    const candidate = { ...project, wires: [...originalWires, ...additions] };
    if (signature(candidate) === baseline) { project.wires = candidate.wires; return; }
  }
  throw new Error('Move or rotation rejected: routing would change electrical connections. Try a smaller move or clear the destination.');
}
function svg(tag, attributes = {}, text) {
  const node = document.createElementNS(NS, tag);
  for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, value);
  if (text !== undefined) node.textContent = text;
  return node;
}
export class SchematicEditor {
  constructor(host, onChange, onSelection = () => {}, onError = () => {}) {
    this.host = host;
    this.onChange = onChange;
    this.onSelection = onSelection;
    this.onError = onError;
    this.project = dividerProject();
    this.undoStack = [];
    this.redoStack = [];
    this.tool = 'select';
    this.selection = null;
    this.view = { x: -180, y: -240, width: 660, height: 500 };
    this.svg = svg('svg', { tabindex: 0, role: 'application', 'aria-label': 'Schematic editor' });
    this.svg.innerHTML = '<defs><pattern id="schematic-grid" width="20" height="20" patternUnits="userSpaceOnUse"><circle cx="0" cy="0" r="1" fill="#c7d5d5"/></pattern></defs>';
    this.background = svg('rect', { fill: 'url(#schematic-grid)', 'pointer-events': 'all' });
    this.layer = svg('g');
    this.preview = svg('path', { fill: 'none', stroke: '#147d5d', 'stroke-width': 2, 'stroke-dasharray': '6 4', 'pointer-events': 'none' });
    this.svg.append(this.background, this.layer, this.preview);
    host.append(this.svg);
    this.svg.addEventListener('pointerdown', (event) => this.pointerDown(event));
    this.svg.addEventListener('pointermove', (event) => this.pointerMove(event));
    this.svg.addEventListener('pointerup', () => this.endGesture());
    this.svg.addEventListener('pointercancel', () => this.cancelGesture());
    this.svg.addEventListener('wheel', (event) => { event.preventDefault(); this.zoom(Math.exp(-event.deltaY * 0.001), this.point(event, false)); }, { passive: false });
    this.svg.addEventListener('contextmenu', (event) => event.preventDefault());
    this.observer = new ResizeObserver(() => { if (this.fitted) this.fit(); else this.render(); });
    this.observer.observe(host);
    this.render();
  }
  snapshot() { return JSON.stringify(this.project); }
  record(before) {
    if (before === this.snapshot()) return;
    this.undoStack.push(before);
    if (this.undoStack.length > 100) this.undoStack.shift();
    this.redoStack = [];
    this.render(); this.onChange();
  }
  edit(action) {
    this.endGesture();
    const before = this.snapshot();
    try { action(); validateProject(this.project); } catch (error) { this.project = JSON.parse(before); this.render(); throw error; }
    this.record(before);
  }
  replace(project) {
    validateProject(project); this.cancelGesture(); this.project = clone(project);
    this.undoStack = []; this.redoStack = []; this.selection = null; this.setTool('select'); this.fit(); this.onSelection(null);
  }
  history(redo = false) {
    this.cancelGesture();
    const from = redo ? this.redoStack : this.undoStack;
    const to = redo ? this.undoStack : this.redoStack;
    if (!from.length) return;
    to.push(this.snapshot()); this.project = JSON.parse(from.pop()); this.selection = null; this.render(); this.onSelection(null); this.onChange();
  }
  setTool(tool) { this.cancelGesture(); this.tool = tool; this.wireStart = null; this.preview.setAttribute('d', ''); this.svg.dataset.tool = tool; }
  select(selection) { this.selection = selection; this.render(); this.onSelection(this.selectedComponent()); }
  selectedComponent() { return this.selection?.type === 'component' ? this.project.components.find((component) => component.reference === this.selection.id) : null; }
  remove() {
    if (!this.selection) return;
    this.edit(() => {
      if (this.selection.type === 'component') this.project.components = this.project.components.filter((component) => component.reference !== this.selection.id);
      else this.project.wires.splice(this.selection.id, 1);
      this.selection = null;
    }); this.onSelection(null);
  }
  transform(action) {
    const component = this.selectedComponent(); if (!component) return;
    this.edit(() => { const before = clone(component); action(component); rerouteAttached(this.project, before, component); });
    this.onSelection(component);
  }
  rotate() { this.transform((component) => { component.rotation = (component.rotation + 90) % 360; }); }
  move(deltaX, deltaY) { this.transform((component) => { component.position = [component.position[0] + deltaX, component.position[1] + deltaY]; }); }
  property(field, value) {
    const component = this.selectedComponent(); if (!component) return;
    this.edit(() => { component[field] = value; });
  }
  point(event, snapped = true) {
    const point = new DOMPoint(event.clientX, event.clientY).matrixTransform(this.svg.getScreenCTM().inverse());
    return snapped ? [snap(point.x), snap(point.y)] : [point.x, point.y];
  }
  pointerDown(event) {
    if (event.button !== 0 && event.button !== 1) return;
    this.svg.focus();
    const point = this.point(event);
    const hit = event.target.closest('[data-component], [data-wire]');
    if (event.button === 1 || this.tool === 'pan' || event.altKey) {
      this.gesture = { type: 'pan', point: this.point(event, false), view: { ...this.view } }; this.fitted = false;
    } else if (hit?.dataset.component && this.tool !== 'wire') {
      this.select({ type: 'component', id: hit.dataset.component });
      this.gesture = { type: 'move', point, before: this.snapshot(), component: clone(this.selectedComponent()) };
    } else if (this.tool === 'wire') {
      if (!this.wireStart) this.wireStart = point;
      else { const start = this.wireStart; this.wireStart = null; this.preview.setAttribute('d', ''); this.edit(() => this.project.wires.push(...route(start, point))); }
    } else if (hit?.dataset.wire !== undefined) this.select({ type: 'wire', id: Number(hit.dataset.wire) });
    else if (Number.isInteger(this.tool)) {
      const kind = this.tool;
      this.edit(() => {
        let counter = this.project.counters[kind];
        do { counter += 1; } while (this.project.components.some((component) => component.reference === `${prefixes[kind]}${counter}`));
        this.project.counters[kind] = counter;
        const component = { kind, reference: `${prefixes[kind]}${counter}`, position: point, rotation: 0, value: defaults[kind], ideality: 1 };
        this.project.components.push(component); this.selection = { type: 'component', id: component.reference };
      }); this.onSelection(this.selectedComponent());
    } else this.select(null);
    if (this.gesture) this.svg.setPointerCapture(event.pointerId);
  }
  pointerMove(event) {
    const point = this.point(event);
    if (this.wireStart) this.preview.setAttribute('d', `M ${this.wireStart} H ${point[0]} V ${point[1]}`);
    if (!this.gesture) return;
    if (this.gesture.type === 'pan') {
      const current = this.point(event, false);
      this.view.x += this.gesture.point[0] - current[0]; this.view.y += this.gesture.point[1] - current[1]; this.render(); return;
    }
    const component = this.selectedComponent();
    component.position = this.gesture.component.position.map((coordinate, index) => coordinate + point[index] - this.gesture.point[index]);
    this.render();
  }
  endGesture() {
    const gesture = this.gesture; this.gesture = null;
    if (gesture?.type === 'move') {
      const component = this.selectedComponent();
      try { rerouteAttached(this.project, gesture.component, component); validateProject(this.project); this.record(gesture.before); }
      catch (error) { this.project = JSON.parse(gesture.before); this.render(); this.onError(error.message); }
    }
  }
  cancelGesture() {
    if (this.gesture?.before) this.project = JSON.parse(this.gesture.before);
    this.gesture = null; this.wireStart = null; this.preview.setAttribute('d', ''); this.render();
  }
  fit() {
    const points = this.project.components.flatMap((component) => [...pins(component), [component.position[0], component.position[1] + 60]]).concat(this.project.wires.flatMap((wire) => [wire.start, wire.end]));
    if (!points.length) points.push([-200, -160], [200, 160]);
    let minimumX = Infinity, maximumX = -Infinity, minimumY = Infinity, maximumY = -Infinity;
    for (const [pointX, pointY] of points) { minimumX = Math.min(minimumX, pointX); maximumX = Math.max(maximumX, pointX); minimumY = Math.min(minimumY, pointY); maximumY = Math.max(maximumY, pointY); }
    const aspect = (this.host.clientWidth || 600) / (this.host.clientHeight || 500);
    const width = Math.max(maximumX - minimumX + 140, (maximumY - minimumY + 140) * aspect, 400);
    this.view = { x: (minimumX + maximumX - width) / 2, y: (minimumY + maximumY - width / aspect) / 2, width, height: width / aspect };
    this.fitted = true; this.render();
  }
  zoom(factor, anchor = [this.view.x + this.view.width / 2, this.view.y + this.view.height / 2]) {
    const width = Math.min(2000000, Math.max(100, this.view.width / factor));
    const ratio = width / this.view.width;
    this.view = { x: anchor[0] - (anchor[0] - this.view.x) * ratio, y: anchor[1] - (anchor[1] - this.view.y) * ratio, width, height: this.view.height * ratio };
    this.fitted = false; this.render();
  }
  render() {
    if (!this.svg) return;
    const view = this.view;
    this.svg.setAttribute('viewBox', `${view.x} ${view.y} ${view.width} ${view.height}`);
    for (const [key, value] of Object.entries({ x: view.x, y: view.y, width: view.width, height: view.height })) this.background.setAttribute(key, value);
    this.layer.replaceChildren();
    const visible = (point, padding = 80) => point[0] >= view.x - padding && point[0] <= view.x + view.width + padding && point[1] >= view.y - padding && point[1] <= view.y + view.height + padding;
    this.project.wires.forEach((wire, index) => {
      if (Math.max(wire.start[0], wire.end[0]) < view.x || Math.min(wire.start[0], wire.end[0]) > view.x + view.width || Math.max(wire.start[1], wire.end[1]) < view.y || Math.min(wire.start[1], wire.end[1]) > view.y + view.height) return;
      const group = svg('g', { 'data-wire': index, class: this.selection?.type === 'wire' && this.selection.id === index ? 'selected' : '' });
      const path = `M ${wire.start} L ${wire.end}`;
      group.append(svg('path', { d: path, class: 'wire-hit' }), svg('path', { d: path, class: 'wire-line' })); this.layer.append(group);
    });
    const anchors = new Map(), horizontal = new Map(), vertical = new Map();
    const anchor = (point, terminal = false) => {
      const key = point.join(',');
      if (!anchors.has(key)) anchors.set(key, { point, terminals: 0 });
      if (terminal) anchors.get(key).terminals += 1;
    };
    for (const component of this.project.components) for (const point of pins(component)) anchor(point, true);
    for (const wire of this.project.wires) {
      anchor(wire.start); anchor(wire.end);
      const upright = wire.start[0] === wire.end[0];
      const index = upright ? vertical : horizontal;
      const fixed = wire.start[upright ? 0 : 1];
      if (!index.has(fixed)) index.set(fixed, { starts: [], ends: [] });
      const line = index.get(fixed), axis = upright ? 1 : 0;
      line.starts.push(Math.min(wire.start[axis], wire.end[axis])); line.ends.push(Math.max(wire.start[axis], wire.end[axis]));
    }
    for (const index of [horizontal, vertical]) for (const line of index.values()) { line.starts.sort((first, second) => first - second); line.ends.sort((first, second) => first - second); }
    const search = (values, value, inclusive) => {
      let low = 0, high = values.length;
      while (low < high) { const middle = Math.floor((low + high) / 2); if (values[middle] < value || (inclusive && values[middle] === value)) low = middle + 1; else high = middle; }
      return low;
    };
    const count = (line, value) => line ? search(line.starts, value, true) - search(line.ends, value, false) : 0;
    for (const { point, terminals } of anchors.values()) {
      if (visible(point) && count(horizontal.get(point[1]), point[0]) + count(vertical.get(point[0]), point[1]) + terminals >= 2) this.layer.append(svg('circle', { cx: point[0], cy: point[1], r: 3.5, class: 'junction' }));
    }
    for (const component of this.project.components) {
      if (!visible(component.position, 150)) continue;
      const group = svg('g', { 'data-component': component.reference, class: this.selection?.id === component.reference ? 'component selected' : 'component', transform: `translate(${component.position})` });
      const symbol = svg('g', { transform: `rotate(${component.rotation})` });
      symbol.append(svg('rect', { x: component.kind === 5 ? -24 : -60, y: -24, width: component.kind === 5 ? 48 : 120, height: 48, class: 'component-hit' }));
      const shapes = [
        'M -60 0 H -30 M -30 -12 H 30 V 12 H -30 Z M 30 0 H 60',
        'M -60 0 H -8 M -8 -22 V 22 M 8 -22 V 22 M 8 0 H 60',
        'M -60 0 H -18 M -18 -20 L 18 0 L -18 20 Z M 18 -20 V 20 M 18 0 H 60',
        'M -60 0 H -26 M 26 0 H 60 M -13 -7 V 7 M -20 0 H -6 M 8 0 H 20',
        'M -60 0 H -26 M 26 0 H 60 M -15 0 H 15 M 5 -8 L 15 0 L 5 8',
        'M 0 0 V 14 M -22 14 H 22 M -14 22 H 14 M -6 30 H 6'
      ];
      if (component.kind === 3 || component.kind === 4) symbol.append(svg('circle', { cx: 0, cy: 0, r: 26, class: 'symbol' }));
      symbol.append(svg('path', { d: shapes[component.kind], class: 'symbol' }));
      for (const pin of component.kind === 5 ? [[0, 0]] : [[-60, 0], [60, 0]]) symbol.append(svg('circle', { cx: pin[0], cy: pin[1], r: 3, class: 'pin' }));
      group.append(symbol);
      const vertical = component.rotation % 180 !== 0;
      group.append(svg('text', { x: vertical ? 34 : 0, y: vertical ? -10 : -52, 'text-anchor': vertical ? 'start' : 'middle', class: 'reference' }, component.reference));
      if (component.kind !== 5) group.append(svg('text', { x: vertical ? 34 : 0, y: vertical ? 10 : -36, 'text-anchor': vertical ? 'start' : 'middle', class: 'component-value' }, `${engineering(component.value)} ${units[component.kind]}`));
      this.layer.append(group);
    }
  }
}