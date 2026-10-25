'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../positron_app/resources/positron-sdk-v1.js'), 'utf8');
let checks = 0;
function check(name, fn) { fn(); checks++; process.stdout.write('PASS ' + name + '\n'); }
function create(grant = true) {
    const calls = [];
    const sandbox = {};
    sandbox.window = sandbox;
    if (grant) sandbox.PositronServices = {request(method, params, callback) {
        calls.push({method, params: JSON.parse(JSON.stringify(params)), callback});
        return 'opaque-token';
    }};
    vm.createContext(sandbox);
    return {sandbox, calls, install() { vm.runInContext(source, sandbox); }};
}
check('no native grant means no SDK', () => {
    const f = create(false);
    assert.throws(() => f.install(), /unavailable/);
    assert.equal(f.sandbox.Positron, undefined);
});
check('versioned frozen facade without paths', () => {
    const f = create(); f.install();
    assert.equal(f.sandbox.Positron.apiVersion, 1);
    assert(Object.isFrozen(f.sandbox.Positron.db));
    assert.deepEqual(Object.keys(f.sandbox.Positron.db), ['query', 'execute', 'transaction']);
});
for (const method of ['query', 'execute']) check(method + ' preserves typed binding and opaque token', () => {
    const f = create(); f.install();
    const params = [{type: 'int64', value: '9007199254740993'}, {type: 'text', value: "中文'\\\np"}, {type: 'null'}, {type: 'real', value: 1.5}];
    let delivered = 0;
    const token = f.sandbox.Positron.db[method]('SELECT ?', params, (ok, value) => {
        assert.equal(ok, true); assert.equal(value, 42); delivered++;
    });
    assert.equal(token, 'opaque-token');
    assert.equal(delivered, 0);
    assert.equal(f.calls[0].method, 'storage.sql');
    assert.deepEqual(f.calls[0].params, {version: 1, method, sql: 'SELECT ?', params});
    f.calls[0].callback(true, 42);
    assert.equal(delivered, 1);
});
check('transaction copies native statement shape without constructing SQL', () => {
    const f = create(); f.install();
    const statements = [{kind: 'execute', sql: 'UPDATE notes SET body=?', params: [{type: 'text', value: 'p'}]}];
    f.sandbox.Positron.db.transaction(statements, () => {});
    assert.deepEqual(f.calls[0].params, {version: 1, method: 'transaction', statements});
});
check('native failure is delivered unchanged and never retried', () => {
    const f = create(); f.install(); let result;
    f.sandbox.Positron.db.query('SELECT ?', [], (ok, value) => { result = {ok, value}; });
    f.calls[0].callback(false, {runtime: -11, db: -1, category: 12});
    assert.deepEqual(result, {ok: false, value: {runtime: -11, db: -1, category: 12}});
    assert.equal(f.calls.length, 1);
});
check('missing callback or malformed convenience arguments submit nothing', () => {
    const f = create(); f.install();
    assert.throws(() => f.sandbox.Positron.db.query('SELECT 1', []), /callback/);
    assert.throws(() => f.sandbox.Positron.db.execute(1, [], () => {}), /SQL/);
    assert.throws(() => f.sandbox.Positron.db.query('SELECT 1', {}, () => {}), /array/);
    assert.throws(() => f.sandbox.Positron.db.transaction({}, () => {}), /array/);
    assert.equal(f.calls.length, 0);
});
check('queue rejection propagates once, no automatic retry', () => {
    const f = create(); f.sandbox.PositronServices.request = () => { throw new Error('full'); }; f.install();
    assert.throws(() => f.sandbox.Positron.db.execute('DELETE FROM notes', [], () => {}), /full/);
});
check('duplicate SDK installation rejected', () => {
    const f = create(); f.install(); assert.throws(() => f.install(), /unavailable/);
});
process.stdout.write('SDK PASS checks=' + checks + ' (wrapper only; native owner/SQL require device gate)\n');
