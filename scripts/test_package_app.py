import tempfile
from pathlib import Path
import unittest
import zipfile
from package_app import build


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        (self.source / 'manifest.json').write_bytes(b'{}')

    def test_exact_resources_and_reproducibility(self):
        (self.source / '中文.txt').write_text('中文', encoding='utf-8')
        (self.source / 'empty').touch()
        first, second = self.root / 'first.pxi', self.root / 'second.pxi'
        build(self.source, first)
        build(self.source, second)
        self.assertEqual(first.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(first) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('中文.txt'), '中文'.encode())
            self.assertEqual(archive.read('empty'), b'')

    def test_existing_output_preserved(self):
        output = self.root / 'exists.pxi'
        output.write_bytes(b'user file')
        with self.assertRaises(FileExistsError):
            build(self.source, output)
        self.assertEqual(output.read_bytes(), b'user file')
        self.assertFalse(list(self.root.glob('*.tmp')))

    def test_nested_output_rejected(self):
        with self.assertRaises(ValueError):
            build(self.source, self.source / 'out.pxi')

    def test_exact_budgets(self):
        for i in range(126):
            (self.source / str(i)).touch()
        with (self.source / 'payload').open('wb') as stream:
            stream.truncate(4 * 1024 * 1024 - 2)
        count, size = build(self.source, self.root / 'exact.pxi')
        self.assertEqual((count, size), (128, 4 * 1024 * 1024))

    def test_manifest_required(self):
        (self.source / 'manifest.json').unlink()
        with self.assertRaises(ValueError):
            build(self.source, self.root / 'out.pxi')

    def test_count_limit(self):
        for i in range(128):
            (self.source / str(i)).touch()
        with self.assertRaises(ValueError):
            build(self.source, self.root / 'out.pxi')
        self.assertFalse((self.root / 'out.pxi').exists())

    def test_byte_limit(self):
        with (self.source / 'large').open('wb') as stream:
            stream.truncate(4 * 1024 * 1024)
        with self.assertRaises(ValueError):
            build(self.source, self.root / 'out.pxi')


if __name__ == '__main__':
    unittest.main()
