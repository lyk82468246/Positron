"""Independently verify closed registry and extracted files against source ZIPs."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import sqlite3
import zipfile
from test_app_install_registry_evidence import resource_digest


def verify(run, emit):
    evidence = run / 'app-pxi'
    stamp = run.name.removesuffix('-app-pxi')
    remote = '\\Temp\\Positron-device-gate\\app-pxi-' + stamp
    expected = {'org.positron.sql_demo': ('sql', 2),
                'org.positron.pxi_unknown': ('unknown', 2),
                'org.positron.pxi_failed': ('bad-migration', 3)}
    with sqlite3.connect((evidence / 'registry-cold.db').resolve().as_uri() + '?mode=ro', uri=True) as db:
        db.row_factory = sqlite3.Row
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        rows = list(db.execute('SELECT * FROM app_installs'))
    assert len(rows) == 3
    files = []
    roots = set()
    for row in rows:
        source, state = expected[row['app_id']]
        assert row['state'] == state and row['generation'] == 2 and row['target_schema'] == 1
        assert row['revision'] == row['api_version'] == row['digest'] == row['approval_policy'] == 1
        assert row['requested'] == row['approved'] == 1
        assert re.fullmatch('[0-9a-f]{32}', row['installation_id']) and int(row['installation_id'], 16)
        assert row['directory'].startswith(remote + '\\apps\\pxi-')
        assert re.fullmatch('[0-9a-f]{32}', row['directory'].rsplit('pxi-', 1)[1])
        assert row['directory'] not in roots
        roots.add(row['directory'])
        local_root = evidence / 'outputs' / source
        with zipfile.ZipFile(evidence / 'inputs' / (source + '.pxi')) as archive:
            for entry in archive.infolist():
                if entry.is_dir():
                    continue
                name = PurePosixPath(entry.filename)
                assert not name.is_absolute() and '..' not in name.parts
                local = local_root.joinpath(*name.parts)
                files.append({'remote': row['directory'] + '\\' + entry.filename.replace('/', '\\'),
                              'local': local.relative_to(evidence).as_posix()})
                if not emit:
                    assert local.read_bytes() == archive.read(entry), entry.filename
        if not emit:
            assert resource_digest(local_root) == row['content_id'], source
    if emit:
        (evidence / 'output-manifest.json').write_text(json.dumps(files, ensure_ascii=False, indent=2), encoding='utf-8')
    else:
        (evidence / 'output-check.txt').write_text('status=PASS\nregistrations=3\nresource_digests=3/3\nzip_bytes=exact\n', encoding='utf-8')
        print('PXI independent output PASS: registrations=3 digests=3/3 ZIP bytes exact')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-root', type=Path, required=True)
    parser.add_argument('--emit-manifest', action='store_true')
    args = parser.parse_args()
    verify(args.run_root, args.emit_manifest)
