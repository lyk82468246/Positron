"""Host-only checks for the isolated manual history dataset generator."""
import hashlib
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

from prepare_app_history_fixture import ROOT, prepare, records, verify


class HistoryFixtureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="history-fixture-test-",
                                                    dir=ROOT / "tmp/device-runs")
        self.addCleanup(self.directory.cleanup)
        self.base = Path(self.directory.name) / "base.db"
        self.output = Path(self.directory.name) / "history-fixture.db"
        with closing(sqlite3.connect(self.base)) as db:
            db.executescript("CREATE TABLE app_visits(id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "url TEXT,title TEXT,visited_utc INTEGER);"
                             "CREATE TABLE app_downloads(id INTEGER PRIMARY KEY);"
                             "CREATE TABLE app_settings(id INTEGER PRIMARY KEY,startup_page TEXT);"
                             "CREATE TABLE app_preferences(id INTEGER PRIMARY KEY,startup_url TEXT,"
                             "language INTEGER,javascript_enabled INTEGER);"
                             "CREATE TABLE fixture_marker(value TEXT);"
                             "INSERT INTO fixture_marker VALUES('must remain opaque');"
                             "INSERT INTO app_settings VALUES(1,'positron://welcome');"
                             "INSERT INTO app_preferences VALUES(1,'positron://welcome',2,0);"
                             "INSERT INTO app_visits VALUES(777,'https://example.com/old','old',1);")

    def test_rows_expected_pages_and_base_unchanged(self):
        digest = hashlib.sha256(self.base.read_bytes()).digest()
        result = prepare(self.base, self.output)
        self.assertEqual(hashlib.sha256(self.base.read_bytes()).digest(), digest)
        by_query = {case["query"]: case for case in result["cases"]}
        for query, pages in (("", [16] * 5), ("alpha", [16, 16, 8]),
                             ("\u4e2d\u6587", [16, 1]), ("Duplicate", [3]),
                             ("%", [1]), ("_", [1]), ("' OR 1=1 --", [1]),
                             ("no-such-fixture-result", [0])):
            self.assertEqual(by_query[query]["pages"], pages)
        with closing(sqlite3.connect(self.output)) as db:
            self.assertEqual(verify(db)["total"], 80)
            self.assertEqual(db.execute("SELECT * FROM fixture_marker").fetchall(),
                             [("must remain opaque",)])
            self.assertEqual(db.execute("SELECT count(*) FROM app_downloads").fetchone(), (0,))
            self.assertEqual(db.execute("SELECT * FROM app_preferences").fetchone(),
                             (1, "positron://newtab", 0, 1))
            self.assertGreater(db.execute("SELECT min(id) FROM app_visits").fetchone()[0], 777)
        saved = json.loads(self.output.with_suffix(".json").read_text(encoding="utf-8"))
        self.assertEqual(saved["database_sha256"], hashlib.sha256(self.output.read_bytes()).hexdigest())

    def test_existing_output_never_overwritten(self):
        prepare(self.base, self.output)
        digest = hashlib.sha256(self.output.read_bytes()).digest()
        with self.assertRaises((ValueError, FileExistsError)):
            prepare(self.base, self.output)
        self.assertEqual(hashlib.sha256(self.output.read_bytes()).digest(), digest)

    def test_input_sidecar_refused(self):
        journal = Path(str(self.base) + "-journal")
        journal.touch()
        with self.assertRaises(ValueError):
            prepare(self.base, self.output)
        self.assertFalse(self.output.exists())

    def test_utf8_budgets_and_empty_title(self):
        self.assertEqual(len(records()), 80)
        self.assertTrue(all(len(row[0].encode("utf-8")) < 1024 and
                            len(row[1].encode("utf-8")) < 256 for row in records()))
        self.assertEqual(sum(row[1] == "" for row in records()), 1)


if __name__ == "__main__":
    unittest.main()
