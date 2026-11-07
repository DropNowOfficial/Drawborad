/* Socket-free state/DOM contract test. Not a browser/layout or native-engine test. */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

class Element {
  constructor(tag = 'div') {
    this.tagName = tag; this.children = []; this.dataset = {}; this.style = {};
    this.attributes = {}; this._value = ''; this.textContent = ''; this.disabled = false;
    this.classList = {toggle: (name, yes) => {
      const names = new Set((this.className || '').split(' ').filter(Boolean));
      if (yes) names.add(name); else names.delete(name);
      this.className = [...names].join(' ');
    }, remove: name => this.classList.toggle(name, false)};
  }
  set value(value) { this._value = String(value); }
  get value() { return this._value; }
  append(...children) { for (const child of children) this.appendChild(child); }
  appendChild(child) {
    this.children.push(child);
    if (this.tagName === 'select' && this.children.length === 1) this.value = child.value;
    return child;
  }
  replaceChildren(...children) { this.children = []; this.value = ''; this.append(...children); }
  setAttribute(key, value) { this.attributes[key] = String(value); }
  getAttribute(key) { return this.attributes[key]; }
  removeAttribute(key) { delete this.attributes[key]; }
  addEventListener() {}
  querySelectorAll(selector) {
    const choices = selector.split(',').map(s => s.trim());
    const found = [];
    for (const child of this.children) {
      if (choices.some(s => s.startsWith('.') ? (child.className || '').split(' ').includes(s.slice(1)) : child.tagName === s)) found.push(child);
      found.push(...child.querySelectorAll(selector));
    }
    return found;
  }
}

const html = fs.readFileSync(path.join(__dirname, '../../cloud/static/index.html'), 'utf8');
const elements = new Map([...html.matchAll(/<([a-z]+)[^>]*\bid="([^"]+)"/g)].map(([, tag, id]) => [id, new Element(tag)]));
const get = id => elements.get(id);
get('nativeColor').value = '#00ff00';
const requests = [], storage = new Map();
let state, failure = null, blobId = 0, previewGate = null;
const context = vm.createContext({
  document: {getElementById: get, createElement: tag => new Element(tag), createTextNode: text => Object.assign(new Element('#text'), {textContent: text})},
  window: {addEventListener() {}}, localStorage: {setItem: (k, v) => storage.set(k, v), getItem: k => storage.get(k) || null, removeItem: k => storage.delete(k)},
  URL: {createObjectURL: () => 'blob:fixture/' + ++blobId, revokeObjectURL() {}},
  setInterval, clearInterval, FormData, console,
  fetch: async (url, options = {}) => {
    requests.push({url, options});
    if (url.endsWith('/capabilities')) return {ok: false, status: 401, headers: {get: () => 'application/json'}, json: async () => ({detail: 'Bearer token required'})};
    if (url.includes('/preview.png')) { if (previewGate) await previewGate; return {ok: true, blob: async () => ({})}; }
    if (url.endsWith('/commands')) {
      const body = JSON.parse(options.body);
      assert.equal(body.expected_revision, state.revision);
      if (failure) {
        const message = failure; failure = null;
        return {ok: false, status: 409, headers: {get: () => 'application/json'}, json: async () => ({detail: {message}})};
      }
      if (body.type === 'recolor_stroke') {
        assert.equal(body.stroke_id, 's1'); assert.equal(body.color, '#00ff00');
      } else {
        assert.equal(body.type, 'set_properties'); assert.equal(body.node_id, 'cat');
        state.nodes.find(n => n.id === 'cat').locked = body.locked;
      }
      state.revision = String(Number(state.revision) + 1); state.dirty = true;
    }
    return {ok: true, headers: {get: () => 'application/json'}, json: async () => structuredClone(state)};
  },
});
vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context, {filename: 'cloud/static/index.html'});
const run = source => vm.runInContext(source, context);
const optionIds = () => get('nativeStrokes').children.map(o => o.value);
const button = id => get('nativeNodes').children.find(r => r.dataset.nodeId === id)?.querySelectorAll('button')[0];
function fixture(id = 'project-one') {
  return {id, revision: '1', dirty: false, stroke_count: '4', nodes: [
    {id: 'root', name: 'Document', kind: 0, parent_id: '0'},
    {id: 'cat', name: 'Cat', kind: 1, parent_id: 'root'},
    {id: 'body', name: 'Body', kind: 3, parent_id: 'cat'},
    {id: 'face', name: 'Face', kind: 2, parent_id: 'cat'},
    {id: 'eyes', name: 'Eyes <b>untrusted name</b>', kind: 3, parent_id: 'face'},
    {id: 'empty', name: 'Empty', kind: 3, parent_id: 'cat'},
    {id: 'ball', name: 'Ball', kind: 1, parent_id: 'root'},
    {id: 'ball-paint', name: 'Ball paint', kind: 3, parent_id: 'ball'},
  ].map(n => ({visible: true, locked: false, ...n})), strokes: [
    {id: 's1', layer_id: 'body', object_id: 'cat', geometry_count: '4'},
    {id: 's2', layer_id: 'body', object_id: 'cat', geometry_count: '4'},
    {id: 's3', layer_id: 'eyes', object_id: 'cat', geometry_count: '4'},
    {id: 's4', layer_id: 'ball-paint', object_id: 'ball', geometry_count: '4'},
  ]};
}
async function show(next) { state = next; context.value = structuredClone(next); await run('nativeShow(value)'); }
function select(id, expected) {
  const selected = button(id);
  assert.ok(selected, 'Missing selectable native row: ' + id);
  const count = requests.length;
  selected.onclick();
  assert.equal(requests.length, count, 'Selection must be UI-only');
  assert.equal(selected.getAttribute('aria-pressed'), 'true');
  assert.deepEqual(optionIds(), expected);
}
(async () => {
  await show(fixture());
  assert.deepEqual(optionIds(), ['s1', 's2', 's3', 's4']);
  assert.ok(get('nativeShowSublayers'), 'Missing painting-sublayers view toggle');
  assert.equal(get('nativeNodes').children.find(r => r.dataset.nodeId === 'body').hidden, true);
  assert.equal(get('nativeNodes').children.find(r => r.dataset.nodeId === 'cat').hidden, false);
  const beforeViewToggle = requests.length;
  get('nativeShowSublayers').checked = true; get('nativeShowSublayers').onchange();
  assert.equal(requests.length, beforeViewToggle);
  assert.equal(get('nativeNodes').children.find(r => r.dataset.nodeId === 'body').hidden, false);
  select('body', ['s1', 's2']);
  assert.match(get('nativeScope').textContent, /Body.*2 of 4 strokes/);
  get('nativeStrokes').value = 's1';
  await get('nativeRecolor').onclick();
  assert.equal(get('nativeStrokes').value, 's1');
  assert.equal(button('body').getAttribute('aria-pressed'), 'true');
  assert.equal(get('nativeDownload').disabled, true);
  assert.equal(get('nativeShowSublayers').checked, true, 'Response refresh reset the view toggle');
  // Recreated rows must be disabled while a slow preview keeps nativeRun busy.
  let releasePreview;
  previewGate = new Promise(resolve => { releasePreview = resolve; });
  const pending = get('nativeRecolor').onclick();
  await new Promise(resolve => setImmediate(resolve));
  for (const control of get('nativeNodes').querySelectorAll('input,button')) assert.equal(control.disabled, true, 'Busy preview exposed an enabled layer control');
  button('ball-paint').onclick();
  assert.equal(button('body').getAttribute('aria-pressed'), 'true', 'Selection changed while busy');
  releasePreview(); previewGate = null; await pending;
  for (const control of get('nativeNodes').querySelectorAll('input,button')) assert.equal(control.disabled, false);
  select('cat', ['s1', 's2', 's3']);
  select('face', ['s3']);
  select('eyes', ['s3']);
  get('nativeShowSublayers').checked = false; get('nativeShowSublayers').onchange();
  assert.equal(button('cat').getAttribute('aria-pressed'), 'true', 'Hiding the selected sublayer must select its visible object');
  assert.deepEqual(optionIds(), ['s1', 's2', 's3']);
  get('nativeShowSublayers').checked = true; get('nativeShowSublayers').onchange();
  select('empty', []);
  for (const id of ['nativeColor', 'nativeStrokes', 'nativeRecolor']) assert.equal(get(id).disabled, true, id);
  assert.equal(get('nativeReplay').disabled, false);
  assert.match(get('nativeStrokeInfo').textContent, /No brush events in this selection/);
  select('ball-paint', ['s4']);
  get('nativeAllStrokes').onclick();
  assert.deepEqual(optionIds(), ['s1', 's2', 's3', 's4']);
  assert.equal(get('nativeStrokes').value, 's4');
  select('body', ['s1', 's2']);
  get('nativeStrokes').value = 's1';
  failure = 'stale_revision';
  await get('nativeRecolor').onclick();
  assert.equal(get('nativeError').textContent, 'stale_revision');
  assert.equal(get('nativeStrokes').value, 's1');
  assert.equal(button('body').getAttribute('aria-pressed'), 'true');
  await show(fixture('project-two'));
  assert.deepEqual(optionIds(), ['s1', 's2', 's3', 's4']);
  assert.equal(get('nativeAllStrokes').getAttribute('aria-pressed'), 'true');
  select('body', ['s1', 's2']);
  state.nodes = state.nodes.filter(n => n.id !== 'body');
  state.strokes = state.strokes.filter(e => e.layer_id !== 'body'); state.stroke_count = '2';
  await show(state);
  assert.deepEqual(optionIds(), ['s3', 's4']);
  assert.equal(get('nativeAllStrokes').getAttribute('aria-pressed'), 'true');
  await get('nativeClose').onclick();
  assert.equal(get('nativeScope').textContent, 'No layer selected');
  assert.equal(get('nativeLayerSummary').textContent, '');
  assert.equal(get('nativeReplayCount').textContent, '0');
  assert.equal(get('nativeAllStrokes').disabled, true);
  assert.equal(storage.size, 0);
  await show({...fixture('empty-project'), strokes: [], stroke_count: '0'});
  assert.equal(get('nativeRecolor').disabled, true);
  assert.equal(get('nativeReplay').disabled, true);
  assert.match(get('nativeLayerSummary').textContent, /4 without strokes/);
  console.log('PASS: layer/object/nested-group selection, stable IDs, scoped controls, empty layers, counts, error recovery, project switch, removed node, close reset.');
})().catch(error => { console.error(error); process.exitCode = 1; });
