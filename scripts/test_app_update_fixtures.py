"""Offline resource facts, not Runtime update or migration execution evidence."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from generate_app_update_fixtures import APP_ID, CASES, generate


class UpdateResourcesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.output = Path(cls.temp.name) / 'fixtures'
        cls.repo = Path(__file__).resolve().parents[1]
        cls.facts = generate(cls.output, cls.repo)

    def test_packages_match_complete_resource_trees(self):
        self.assertEqual(set(self.facts), set(CASES))
        for case, facts in self.facts.items():
            with self.subTest(case=case), zipfile.ZipFile(self.output / facts['package']) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(set(archive.namelist()), set(facts['files']))
                for key, info in facts['files'].items():
                    body = archive.read(key)
                    self.assertEqual(body, (self.output / case / key).read_bytes())
                    self.assertEqual(len(body), info['bytes'])
                    self.assertEqual(hashlib.sha256(body).hexdigest(), info['sha256'])

    def test_same_schema_resources_preserve_migration_and_author_logic(self):
        old, new = self.facts['old'], self.facts['resources-v2']
        self.assertEqual(old['manifest']['id'], APP_ID)
        self.assertEqual(new['manifest']['id'], APP_ID)
        self.assertEqual(old['manifest']['version'], 1)
        self.assertEqual(new['manifest']['version'], 2)
        for field in ('permissions', 'migrations', 'runtimeApiVersion'):
            self.assertEqual(old['manifest'][field], new['manifest'][field])
        for key in ('schema/1.sql', 'notes.js', 'notes.css'):
            self.assertEqual(old['files'][key], new['files'][key])
        for case, version in [('old', 1), ('resources-v2', 2)]:
            self.assertIn(('Notes resource v%d' % version).encode(),
                          (self.output / case / 'index.html').read_bytes())
        self.assertNotEqual(old['files']['index.html'], new['files']['index.html'])

    def test_candidate_differences_are_explicit_facts(self):
        self.assertIn('schema/2.sql', self.facts['schema-increase']['files'])
        self.assertNotEqual(self.facts['old']['files']['schema/1.sql'],
                            self.facts['migration-rewrite']['files']['schema/1.sql'])
        self.assertEqual(self.facts['permissions-change']['manifest']['permissions'], [])
        self.assertEqual(self.facts['version-unchanged']['manifest']['version'], 1)
        self.assertEqual(self.facts['version-zero']['manifest']['version'], 0)
        self.assertNotEqual(self.facts['foreign-id']['manifest']['id'], APP_ID)
        self.assertEqual(self.facts['future-api']['manifest']['runtimeApiVersion'], 2)
        self.assertNotIn('index.html', self.facts['missing-entry']['files'])

    def test_reproducible_and_non_overwriting(self):
        second = Path(self.temp.name) / 'second'
        generate(second, self.repo)
        self.assertEqual((self.output / 'facts.json').read_bytes(),
                         (second / 'facts.json').read_bytes())
        for case in CASES:
            self.assertEqual((self.output / (case + '.pxi')).read_bytes(),
                             (second / (case + '.pxi')).read_bytes())
        with self.assertRaises(FileExistsError):
            generate(self.output, self.repo)
        self.assertEqual(json.loads((self.output / 'facts.json').read_text(encoding='utf-8')),
                         self.facts)


if __name__ == '__main__':
    unittest.main()
