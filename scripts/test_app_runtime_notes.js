/* SDK + example UI mapping only; does not simulate the native SQL engine. */
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const root = path.join(__dirname, '../positron_app');
const sdk = fs.readFileSync(path.join(root, 'resources/positron-sdk-v1.js'), 'utf8');
const code = fs.readFileSync(path.join(root, 'examples/notes/notes.js'), 'utf8');
const html = fs.readFileSync(path.join(root, 'examples/notes/index.html'), 'utf8');
let checks = 0;
function check(name, fn) { fn(); checks++; process.stdout.write('PASS ' + name + '\n'); }
function fixture(grant = true) {
    const elements = {}, calls = [];
    function create(id, parent = null) {
        elements[id] = {id, parent, value: '', textContent: '', disabled: false, handlers: {}, listenerCount: 0,
            addEventListener(name, handler) { this.handlers[name] = handler; this.listenerCount++; },
            set innerHTML(value) {
                this.markup = value;
                for (const match of value.matchAll(/id="([^"]+)"/g)) create(match[1], this.id);
            }};
    }
    for (const match of html.matchAll(/id="([^"]+)"/g)) create(match[1]);
    const sandbox = {document: {getElementById(id) { return elements[id] || null; }}};
    sandbox.window = sandbox;
    if (grant) sandbox.PositronServices = {request(method, params, callback) {
        calls.push({method, params: JSON.parse(JSON.stringify(params)), callback}); return 'token';
    }};
    vm.createContext(sandbox);
    if (grant) vm.runInContext(sdk, sandbox);
    vm.runInContext(code, sandbox);
    return {calls, elements, click(id) {
        let target = elements[id];
        while (target) {
            if (target.handlers.click) target.handlers.click({target: elements[id]});
            target = elements[target.parent];
        }
    },
        reply(rows = [], index = calls.length - 1, ok = true) {
            calls[index].callback(ok, {rowCount: rows.length, rows: rows.map(values => ({statement: 0, values}))});
        }};
}
const text = value => ({type: 'text', value});
const integer = value => ({type: 'int64', value});
check('no grant cannot construct a fake storage connection', () => {
    const f = fixture(false);
    assert.equal(f.calls.length, 0); assert.match(f.elements.status.textContent, /not granted/);
    assert(f.elements.save.disabled);
});
check('empty list and bound first cursor', () => {
    const f = fixture(); assert.equal(f.calls[0].method, 'storage.sql');
    assert.deepEqual(f.calls[0].params.params, [integer('0'), text('%%'), text('%%')]);
    f.reply(); assert.match(f.elements.rows.markup, /No matching/);
    assert(f.elements.previous.disabled); assert(f.elements.next.disabled);
});
check('new note binds Unicode/quotes; no SQL interpolation', () => {
    const f = fixture(); f.reply();
    f.elements.title.value = "中文' title"; f.elements.body.value = 'body\np'; f.click('save');
    assert.equal(f.calls[1].params.sql, 'INSERT INTO notes(title,body) VALUES(?,?)');
    assert.deepEqual(f.calls[1].params.params, [text("中文' title"), text('body\np')]);
    assert(f.elements.save.disabled); f.reply(); assert.equal(f.calls.length, 3);
});
check('list text is escaped and edit preserves large int64', () => {
    const f = fixture(); f.reply([[integer('9007199254740993'), text('<script>&"')]]);
    assert(!f.elements.rows.markup.includes('<script>'));
    assert(f.elements.rows.markup.includes('&lt;script&gt;&amp;&quot;'));
    f.click('edit-0'); assert.deepEqual(f.calls[1].params.params, [integer('9007199254740993')]);
    f.reply([[text('title'), text('old body')]]); f.elements.body.value = 'new'; f.click('save');
    assert.match(f.calls[2].params.sql, /^UPDATE/);
    assert.deepEqual(f.calls[2].params.params, [text('title'), text('new'), integer('9007199254740993')]);
});
check('delete requires a second explicit click', () => {
    const f = fixture(); f.reply([[integer('2'), text('title')]]); f.click('edit-0');
    f.reply([[text('title'), text('body')]]); f.click('delete'); assert.equal(f.calls.length, 2);
    f.click('delete'); assert.equal(f.calls.length, 3);
    assert.deepEqual(f.calls[2].params.params, [integer('2')]); assert.match(f.calls[2].params.sql, /^DELETE/);
});
check('native error preserves editable values and does not replay', () => {
    const f = fixture(); f.reply(); f.elements.title.value = 'kept'; f.click('save');
    f.reply([], 1, false); assert.equal(f.elements.title.value, 'kept');
    assert.equal(f.calls.length, 2); assert.match(f.elements.status.textContent, /failed/);
});
check('paging uses exact typed string and search resets cursor', () => {
    const f = fixture();
    f.reply(Array.from({length: 8}, (_, n) => [integer('900719925474099' + n), text('row')]));
    f.click('next'); assert.equal(f.calls[1].params.params[0].value, '9007199254740997');
    f.reply(); f.click('previous'); assert.equal(f.calls[2].params.params[0].value, '0');
    f.reply(); f.elements.search.value = "中文'"; f.click('search-button');
    assert.deepEqual(f.calls[3].params.params, [integer('0'), text("%中文'%"), text("%中文'%")]);
});
check('repeated list replacement keeps one delegated row listener', () => {
    const f = fixture();
    for (let i = 0; i < 20; i++) {
        f.reply([[integer('1'), text('row')]]);
        f.click('search-button');
    }
    assert.equal(f.elements.rows.listenerCount, 1);
    assert.equal(f.elements['edit-0'].listenerCount, 0);
});
process.stdout.write('NOTES PASS checks=' + checks + ' (native UI/SQL require device gate)\n');
