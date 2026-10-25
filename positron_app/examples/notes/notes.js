/* Example business/UI code; SQL policy, identity and transactions stay native. */
(function () {
    'use strict';
    var db = window.Positron && Positron.db;
    var cursor = '0', lastId = '0', previous = [], filter = '', selected = null;
    var rowIds = [];
    var busy = false, count = 0, deleteArmed = false;
    var ids = ['search', 'search-button', 'previous', 'next', 'title', 'body', 'save', 'new', 'delete'];
    function element(id) { return document.getElementById(id); }
    function integer(value) { return {type: 'int64', value: value}; }
    function text(value) { return {type: 'text', value: value}; }
    function message(value) { element('status').textContent = value; }
    function controls() {
        var i;
        for (i = 0; i < ids.length; i++) element(ids[i]).disabled = busy;
        element('previous').disabled = busy || !previous.length;
        element('next').disabled = busy || count !== 8;
        element('delete').disabled = busy || selected === null;
        for (i = 0; i < count; i++) element('edit-' + i).disabled = busy;
    }
    function request(method, sql, params, done) {
        if (busy) return;
        busy = true; controls(); message('Working...');
        try {
            db[method](sql, params, function (ok, value) {
                busy = false; controls();
                if (!ok) { message('Storage request failed. Existing notes were kept.'); return; }
                done(value);
            });
        } catch (error) {
            busy = false; controls(); message('Request unavailable or too large. Please try a smaller entry.');
        }
    }
    function escape(value) {
        return value.replace(/[&<>"']/g, function (ch) {
            return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[ch];
        });
    }
    function editNote(id) {
        request('query', 'SELECT title,body FROM notes WHERE id=?', [integer(id)], function (value) {
                if (value.rowCount !== 1) { message('This note no longer exists.'); return; }
                selected = id; deleteArmed = false;
                element('title').value = value.rows[0].values[0].value;
                element('body').value = value.rows[0].values[1].value;
                element('editor-heading').textContent = 'Edit note';
                element('delete').textContent = 'Delete selected';
                controls(); message('Editing note ' + id + '.');
        });
    }
    /* One stable listener: replacing the list must not accumulate native
     * listeners/wrappers or exhaust the framework's bounded event table. */
    element('rows').addEventListener('click', function (event) {
        var id = event.target && event.target.id, index;
        if (!id || id.slice(0, 5) !== 'edit-') return;
        index = parseInt(id.slice(5), 10);
        if (index >= 0 && index < rowIds.length && id === 'edit-' + index) editNote(rowIds[index]);
    });
    function load(nextCursor, nextPrevious, nextFilter) {
        var pattern = '%' + nextFilter + '%';
        request('query', 'SELECT id,title FROM notes WHERE id>? AND (title LIKE ? OR body LIKE ?) ORDER BY id LIMIT 8',
            [integer(nextCursor), text(pattern), text(pattern)], function (value) {
                var html = '', i, row;
                cursor = nextCursor; previous = nextPrevious; filter = nextFilter; count = value.rowCount;
                rowIds = [];
                for (i = 0; i < count; i++) {
                    row = value.rows[i].values;
                    rowIds.push(row[0].value);
                    html += '<p><button type="button" id="edit-' + i + '">Edit</button> ' + escape(row[1].value) + '</p>';
                }
                element('rows').innerHTML = html || '<p>No matching notes.</p>';
                lastId = count ? value.rows[count - 1].values[0].value : cursor;
                controls(); message('Showing ' + count + ' notes.');
            });
    }
    function clearEditor() {
        selected = null; deleteArmed = false;
        element('title').value = ''; element('body').value = '';
        element('editor-heading').textContent = 'New note';
        element('delete').textContent = 'Delete selected';
        controls();
    }
    element('search-button').addEventListener('click', function () {
        load('0', [], element('search').value);
    });
    element('next').addEventListener('click', function () {
        if (!busy && count === 8) load(lastId, previous.concat([cursor]), filter);
    });
    element('previous').addEventListener('click', function () {
        if (!busy && previous.length) load(previous[previous.length - 1], previous.slice(0, -1), filter);
    });
    element('new').addEventListener('click', function () { if (!busy) { clearEditor(); message('New note.'); } });
    element('save').addEventListener('click', function () {
        var title = element('title').value, body = element('body').value;
        if (busy) return;
        if (!title.length || title.length > 40 || body.length > 512) { message('Use a title of 1-40 characters and text up to 512 characters.'); return; }
        var params = [text(title), text(body)];
        var sql = 'INSERT INTO notes(title,body) VALUES(?,?)';
        if (selected !== null) { sql = 'UPDATE notes SET title=?,body=? WHERE id=?'; params.push(integer(selected)); }
        request('execute', sql, params, function () { clearEditor(); load('0', [], filter); });
    });
    element('delete').addEventListener('click', function () {
        if (busy || selected === null) return;
        if (!deleteArmed) { deleteArmed = true; element('delete').textContent = 'Confirm delete'; message('Click Confirm delete again to delete this note.'); return; }
        request('execute', 'DELETE FROM notes WHERE id=?', [integer(selected)], function () {
            clearEditor(); load('0', [], filter);
        });
    });
    if (!db) { busy = true; controls(); message('Native SQL permission was not granted.'); return; }
    load('0', [], '');
}());
