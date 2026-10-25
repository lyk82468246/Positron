/* Real author-side SDK consumer. No copied parser, queue or authorization code. */
(function () {
    'use strict';
    var status = document.getElementById('status');
    var seed = window.runtimeFixtureSeed;
    var db = window.Positron && Positron.db;
    function fail(detail) { status.textContent = 'runtime-sql-failed ' + detail; }
    function expect(ok, condition, next, detail) {
        if (!ok || !condition) { fail(detail); return; }
        next();
    }
    function id(value) { return {type: 'int64', value: String(value)}; }
    function seedBatch(start) {
        var statements = [], i;
        if (start === 33) { verifyRows(); return; }
        for (i = start; i < start + 8; i++) {
            statements.push({kind: 'execute', sql: 'INSERT INTO notes(id,body) VALUES(?,?)',
                params: [id(i), {type: 'text', value: seed + ':' + i + ":中文'p"}]});
        }
        db.transaction(statements, function (ok) {
            expect(ok, true, function () { seedBatch(start + 8); }, 'seed');
        });
    }
    function verifyRows() {
        db.query('SELECT id,body FROM notes WHERE id>? ORDER BY id LIMIT 16', [id(0)], function (ok, result) {
            expect(ok, ok && result.rowCount === 16 && result.rows[0].values[0].value === '1' &&
                result.rows[0].values[1].value === seed + ":1:中文'p", function () {
                db.query('SELECT id FROM notes WHERE id>? ORDER BY id LIMIT 16', [id(16)], function (yes, page) {
                    expect(yes, yes && page.rowCount === 16 && page.rows[15].values[0].value === '32',
                        exactInteger, 'page-two');
                });
            }, 'page-one-or-isolation');
        });
    }
    function exactInteger() {
        db.query('SELECT ?', [id('9007199254740993')], function (ok, value) {
            expect(ok, ok && value.rows[0].values[0].value === '9007199254740993', rollback, 'int64');
        });
    }
    function rollback() {
        db.transaction([
            {kind: 'execute', sql: 'INSERT INTO notes(id,body) VALUES(1000,?)', params: [{type: 'text', value: 'must rollback'}]},
            {kind: 'execute', sql: 'INSERT INTO notes(id,body) VALUES(1,?)', params: [{type: 'text', value: 'duplicate'}]}
        ], function (ok) {
            if (ok) { fail('rollback-accepted'); return; }
            db.query('SELECT count(*) FROM notes WHERE id=1000', [], function (yes, value) {
                expect(yes, yes && value.rows[0].values[0].value === '0', rowLimit, 'rollback-state');
            });
        });
    }
    function rowLimit() {
        db.query('SELECT id FROM notes ORDER BY id LIMIT 17', [], function (ok) {
            if (ok) { fail('row-budget'); return; }
            db.execute('DROP TABLE notes', [], function (accepted) {
                if (accepted) { fail('ddl-grant'); return; }
                backpressure();
            });
        });
    }
    function backpressure() {
        var replies = 0, refused = 0, i;
        function answer(ok, value) {
            if (!ok || value.rows[0].values[0].value !== '32') { fail('queue-result'); return; }
            replies++;
            if (replies === 8 && refused === 1) status.textContent = 'runtime-sql-ready seed=' + seed + ' rows=32';
        }
        for (i = 0; i < 9; i++) {
            try { db.query('SELECT count(*) FROM notes', [], answer); }
            catch (error) { refused++; }
        }
        if (refused !== 1) fail('queue-capacity');
    }
    if (!db || Positron.apiVersion !== 1) { fail('missing-sdk'); return; }
    db.query('SELECT count(*) FROM notes', [], function (ok, value) {
        if (!ok) { fail('initial-query'); return; }
        var count = value.rows[0].values[0].value;
        if (count === '0') seedBatch(1);
        else if (count === '32') verifyRows();
        else fail('persistent-count-' + count);
    });
}());
