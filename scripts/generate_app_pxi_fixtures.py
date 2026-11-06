"""Create isolated PXI consumer inputs; never write installed directories or DBs."""
import argparse
import io
import json
from pathlib import Path
import zipfile


def package(items):
    output = io.BytesIO()
    with zipfile.ZipFile(output, 'w') as archive:
        for name, body, method in items:
            item = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            item.compress_type = method
            archive.writestr(item, body)
    return output.getvalue()


def manifest(app_id, migrations=()):
    return json.dumps(dict(manifestVersion=1, id=app_id, name='PXI consumer', version=1,
                          runtimeApiVersion=1, entry='index.html',
                          permissions=['storage.sql'] if migrations else [],
                          migrations=[dict(version=i + 1, file='schema/%d.sql' % (i + 1))
                                      for i in range(len(migrations))]), separators=(',', ':')).encode()


def generate(output, repo):
    output.mkdir(parents=True, exist_ok=False)
    valid = [('index.html', b'<!doctype html><html><body>PXI fixture</body></html>', 0),
             ('manifest.json', manifest('org.positron.pxi_demo'), 8),
             ('nested/', b'', 0), ('nested/中文.txt', '中文 resource'.encode(), 8),
             ('empty.bin', b'', 8), ('unreferenced.bin', b'complete-view', 0)]
    corpus = {'valid': package(valid), 'missing-manifest': package(valid[:1]),
              'traversal': package([('../escape.txt', b'no', 0)]),
              'collision': package([('a.txt', b'A', 0), ('A.TXT', b'B', 0)]),
              'long-native': package(valid[:1] + [('x' * 220, b'long', 0)]),
              'exact-count': package([('empty-%03d' % i, b'', 0) for i in range(128)]),
              'over-count': package([('empty-%03d' % i, b'', 0) for i in range(129)]),
              'exact-bytes': package([('payload.bin', b'x' * (4 * 1024 * 1024), 8)]),
              'over-bytes': package([('payload.bin', b'x' * (4 * 1024 * 1024 + 1), 8)])}
    crc_items = valid + [('last.bin', b'bad-crc-payload', 0)]
    corrupt = bytearray(package(crc_items))
    corrupt[corrupt.index(b'bad-crc-payload')] ^= 1
    corpus['crc'] = bytes(corrupt)
    fixture = repo / 'positron_app/fixtures/runtime-sql'
    corpus['sql'] = package([(p.relative_to(fixture).as_posix(), p.read_bytes(), 8)
                              for p in sorted(fixture.rglob('*')) if p.is_file()])
    for name, app_id, sql in [('unknown', 'org.positron.pxi_unknown', 'CREATE TABLE marker(id INTEGER);'),
                              ('bad-migration', 'org.positron.pxi_failed', 'DROP TABLE missing;')]:
        corpus[name] = package([('index.html', valid[0][1], 0), ('manifest.json', manifest(app_id, (sql,)), 8),
                                ('schema/1.sql', sql.encode(), 8)])
    for name, body in corpus.items():
        (output / (name + '.pxi')).write_bytes(body)
    # Actual author resources, a separate identity from the user's old Notes.
    notes = repo / 'positron_app/examples/notes'
    items = []
    for p in sorted(notes.rglob('*')):
        if p.is_file():
            body = p.read_bytes()
            if p.name == 'manifest.json':
                value = json.loads(body)
                value['id'] = 'org.positron.notes_pxi'
                body = json.dumps(value, separators=(',', ':')).encode()
            items.append((p.relative_to(notes).as_posix(), body, 8))
    (output / 'notes.pxi').write_bytes(package(items))
    print('PXI inputs:', len(corpus) + 1)


if __name__ == '__main__':
    args = argparse.ArgumentParser(description=__doc__)
    args.add_argument('--output', type=Path, required=True)
    parsed = args.parse_args()
    generate(parsed.output, Path(__file__).resolve().parents[1])
