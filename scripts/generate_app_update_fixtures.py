"""Create author resources and byte facts for public Runtime update tests.

No native registration, SQL execution, update decision, or user data is touched.
"""
import argparse
import hashlib
import json
from pathlib import Path

from package_app import build


APP_ID = 'org.positron.updatenotes'
CASES = ('old', 'resources-v2', 'schema-increase', 'migration-rewrite',
         'permissions-change', 'version-unchanged', 'version-zero',
         'foreign-id', 'future-api', 'missing-entry')


def generate(output, repo):
    output.mkdir(parents=True, exist_ok=False)
    source = repo / 'positron_app/examples/notes'
    original = {p.relative_to(source).as_posix(): p.read_bytes()
                for p in sorted(source.rglob('*')) if p.is_file()}
    catalog = {}
    for case in CASES:
        files = dict(original)
        manifest = json.loads(files['manifest.json'])
        manifest['id'] = APP_ID
        manifest['version'] = 1 if case in ('old', 'version-unchanged') else 2
        if case == 'version-zero':
            manifest['version'] = 0
        elif case == 'foreign-id':
            manifest['id'] = 'org.positron.foreignnotes'
        elif case == 'future-api':
            manifest['runtimeApiVersion'] = 2
        elif case == 'permissions-change':
            manifest['permissions'] = []
            manifest['migrations'] = []
        elif case == 'schema-increase':
            manifest['migrations'].append({'version': 2, 'file': 'schema/2.sql'})
            files['schema/2.sql'] = b'CREATE TABLE update_marker(id INTEGER);\n'
        elif case == 'migration-rewrite':
            files['schema/1.sql'] = b'-- changed historical bytes\n' + files['schema/1.sql']
        files['manifest.json'] = json.dumps(manifest, sort_keys=True,
                                           separators=(',', ':')).encode('utf-8')
        marker = 1 if case == 'old' else 2
        html = files['index.html'].decode('utf-8')
        html = html.replace('<title>Positron Notes</title>',
                            '<title>Update fixture v%d</title>' % marker)
        html = html.replace('<body>', '<body><p id="update-fixture-version">'
                            'Notes resource v%d</p>' % marker, 1)
        files['index.html'] = html.encode('utf-8')
        if case == 'missing-entry':
            del files['index.html']
        directory = output / case
        directory.mkdir()
        for key, body in sorted(files.items()):
            target = directory / key
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        build(directory, output / (case + '.pxi'))
        catalog[case] = {
            'directory': case, 'package': case + '.pxi',
            'manifest': manifest,
            'files': {key: {'bytes': len(body),
                            'sha256': hashlib.sha256(body).hexdigest()}
                      for key, body in sorted(files.items())}}
    (output / 'facts.json').write_text(json.dumps(catalog, ensure_ascii=False,
                                                 sort_keys=True, indent=2), encoding='utf-8')
    return catalog


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    facts = generate(args.output, Path(__file__).resolve().parents[1])
    print('Update fixture resources generated:', len(facts),
          '(byte facts only; Runtime decisions NOT RUN)')
