/* Isolated test driver; uses the real Notes handlers and native SQL SDK.
 * Only the gate's copied index includes this file. Never ship it as Notes. */
(function () {
    'use strict';
    var seed = window.notesAcceptanceSeed, round = window.notesAcceptanceRound;
    var stage = 0, pending = false, failed = false, page = 0, reopened = false;
    var db = window.Positron && Positron.db;
    var saved = seed + " saved 中文'p<&", edited = seed + ' edited 中文\np';
    function el(id) { return document.getElementById(id); }
    function text(value) { return {type: 'text', value: value}; }
    function integer(value) { return {type: 'int64', value: String(value)}; }
    function fail(detail) { failed = true; el('acceptance-status').textContent = 'notes-failed stage=' + stage + ' ' + detail; }
    function expect(condition, detail) { if (!condition) throw new Error(detail); }
    function title(n) { return seed + ':' + (n < 10 ? '0' : '') + n + " 中文'p<&"; }
    function body(n) { return seed + ' body-only-' + n + '\n中文 p'; }
    function click(id) { expect(!el(id).disabled, 'disabled-' + id); el(id).click(); }
    function search(value) { el('search').value = value; click('search-button'); }
    function query(sql, params, done) {
        pending = true;
        db.query(sql, params, function (ok, result) {
            pending = false;
            try { expect(ok, 'query'); done(result); } catch (error) { fail(String(error)); }
        });
    }
    function seedBatch(first) {
        var statements = [], i;
        if (first > 25) { pending = false; return; }
        for (i = first; i <= 25 && i < first + 8; i++) {
            statements.push({kind: 'execute', sql: 'INSERT INTO notes(id,title,body) VALUES(?,?,?)',
                params: [integer(i), text(title(i)), text(body(i))]});
        }
        db.transaction(statements, function (ok) {
            if (!ok) { fail('seed'); return; }
            seedBatch(first + 8);
        });
    }
    function verifyPage(number) {
        var rows = el('rows').textContent, i, first = number * 8 + 1;
        var count = number < 3 ? 8 : (reopened ? 2 : 1);
        expect(el('status').textContent === 'Showing ' + count + ' notes.', 'page-count-' + number);
        for (i = first; i < first + count && i <= 25; i++) expect(rows.indexOf(title(i)) >= 0, 'row-' + i);
        if (number === 3 && reopened) expect(rows.indexOf(saved) >= 0, 'saved-page');
        expect(el('previous').disabled === (number === 0), 'previous-' + number);
        expect(el('next').disabled === (number === 3), 'next-' + number);
    }
    function finish() {
        query('SELECT count(*) FROM notes', [], function (result) {
            expect(result.rows[0].values[0].value === '26', 'final-count');
            query('SELECT title,body FROM notes WHERE id=26', [], function (value) {
                expect(value.rowCount === 1 && value.rows[0].values[0].value === saved &&
                    value.rows[0].values[1].value === edited, 'persistent-edit-or-isolation');
                el('acceptance-status').textContent = 'notes-ready seed=' + seed + ' rows=26';
                stage = 99;
            });
        });
    }
    window.notesAcceptanceTick = function () {
        var status;
        if (failed || pending || stage === 99) return;
        try {
            status = el('status').textContent;
            if (status === 'Opening...' || status === 'Working...') return;
            expect(status.indexOf('failed') < 0 && status.indexOf('unavailable') < 0, 'notes-status-' + status);
            switch (stage) {
            case 0:
                expect(db && (seed === 'one' || seed === 'two'), 'sdk-or-seed');
                stage = 1;
                query('SELECT count(*) FROM notes', [], function (value) {
                    var count = value.rows[0].values[0].value;
                    if (round === 1) {
                        expect(count === '0', 'fresh-root-not-empty');
                        pending = true; seedBatch(1);
                    } else {
                        expect(round === 2 && count === '26', 'reopen-count');
                        reopened = true;
                    }
                });
                break;
            case 1:
                /* Dispatch from the next UI tick, outside a service callback.
                 * Do not dirty the retained layout with progress text first. */
                search(''); stage = 2; break;
            case 2:
                verifyPage(page);
                if (page < 3) { page++; click('next'); }
                else { click('previous'); stage = 3; }
                break;
            case 3:
                verifyPage(2); search(seed + ' body-only-17'); stage = 4; break;
            case 4:
                expect(el('status').textContent === 'Showing 1 notes.' &&
                    el('rows').textContent.indexOf(title(17)) >= 0, 'body-search');
                click('edit-0'); stage = 5; break;
            case 5:
                expect(el('title').value === title(17) && el('body').value === body(17), 'edit-seed');
                search(seed === 'one' ? 'two:' : 'one:'); stage = 6; break;
            case 6:
                expect(el('status').textContent === 'Showing 0 notes.', 'foreign-root-visible');
                search(''); stage = reopened ? 20 : 7; break;
            case 7:
                click('new'); el('title').value = saved; el('body').value = seed + ' original';
                click('save'); stage = 8; break;
            case 8:
                search("saved 中文'"); stage = 9; break;
            case 9:
                expect(el('status').textContent === 'Showing 1 notes.' &&
                    el('rows').textContent.indexOf(saved) >= 0, 'create-or-escape');
                click('edit-0'); stage = 10; break;
            case 10:
                expect(el('title').value === saved && el('body').value === seed + ' original', 'created-values');
                el('body').value = edited; click('save'); stage = 11; break;
            case 11:
                click('edit-0'); stage = 12; break;
            case 12:
                expect(el('body').value === edited, 'updated-body');
                click('new'); el('title').value = seed + ' temporary'; el('body').value = 'delete me';
                click('save'); stage = 13; break;
            case 13:
                search('temporary'); stage = 14; break;
            case 14:
                expect(el('status').textContent === 'Showing 1 notes.', 'temporary-created');
                click('edit-0'); stage = 15; break;
            case 15:
                click('delete');
                expect(el('delete').textContent === 'Confirm delete' &&
                    el('rows').textContent.indexOf(seed + ' temporary') >= 0, 'delete-confirm');
                stage = 16;
                query('SELECT count(*) FROM notes', [], function (value) {
                    expect(value.rows[0].values[0].value === '27', 'first-delete-mutated');
                });
                break;
            case 16:
                click('delete'); stage = 17; break;
            case 17:
                expect(el('status').textContent === 'Showing 0 notes.', 'delete-not-visible');
                search("saved 中文'"); stage = 20; break;
            case 20:
                if (reopened) { search("saved 中文'"); stage = 21; }
                else stage = 21;
                break;
            case 21:
                expect(el('status').textContent === 'Showing 1 notes.', 'saved-reload');
                click('edit-0'); stage = 22; break;
            case 22:
                expect(el('title').value === saved && el('body').value === edited, 'final-editor');
                finish(); break;
            default: fail('unexpected-stage');
            }
            if (!failed && stage !== 99) el('acceptance-status').textContent = 'notes-running stage=' + stage;
        } catch (error) { fail(String(error)); }
    };
}());
