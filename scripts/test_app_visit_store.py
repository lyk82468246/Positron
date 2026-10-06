"""Offline SQLite checks of the EXE-private visit store's exact SQL.

Host sqlite3 and a model of the public migration guard only: these tests do
not compile/run the C worker, load Positron DLLs, or provide WM6 evidence.
Threading, heap ownership, DLL errors and device durability need later gates.
"""
import ast
from contextlib import closing
from pathlib import Path
import re
import sqlite3
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "positron_app/app_settings_store.c").read_text(encoding="utf-8")
HEADER = (ROOT / "positron_app/app_settings_store.h").read_text(encoding="utf-8")


def c_string(name):
    match = re.search(r"static const char " + name + r"\[\] =\s*"
                      r'((?:"(?:\\.|[^"\\])*"\s*)+);', SOURCE)
    if match is None:
        raise AssertionError("Missing C SQL constant: " + name)
    return "".join(ast.literal_eval(part) for part in
                   re.findall(r'"(?:\\.|[^"\\])*"', match.group(1)))


SETTINGS_SCHEMA = c_string("g_settings_schema")
SETTINGS_READ = c_string("g_settings_read_sql")
SETTINGS_WRITE = c_string("g_settings_write_sql")
SETTINGS_EXISTS = c_string("g_settings_exists_sql")
VISITS_SCHEMA = c_string("g_visits_schema")
VISITS_EXISTS = c_string("g_visits_exists_sql")
ADD = c_string("g_visits_add_sql")
PRUNE = c_string("g_visits_prune_sql")
READ = c_string("g_visits_read_sql")
CLEAR = c_string("g_visits_clear_sql")


def apply_migration_model(db, current, target, script):
    """Public guard model, not DLL metadata SQL or a DLL implementation test."""
    if target < current or (target == current and script):
        raise ValueError("Public migration guard refuses this version/script")
    if target == current:
        return current
    try:
        db.executescript("BEGIN;" + script + "COMMIT;")
    except sqlite3.Error:
        db.rollback()
        raise
    return target


def initialize_model(db, current):
    """Mirror the C app-table probes and single target-v2 migration call."""
    settings = db.execute(SETTINGS_EXISTS).fetchone()[0]
    visits = db.execute(VISITS_EXISTS).fetchone()[0]
    if visits and not settings:
        raise ValueError("Visits without settings")
    if settings:
        rows = db.execute(SETTINGS_READ).fetchall()
        if (len(rows) != 1 or rows[0][0] != 1 or rows[0][1] not in
                ("positron://newtab", "positron://welcome", "positron://controls")):
            raise ValueError("Invalid settings projection")
    script = ("" if settings else SETTINGS_SCHEMA) + ("" if visits else VISITS_SCHEMA)
    return apply_migration_model(db, current, 2, script)


def add_visit(db, url, title, utc):
    with db:
        db.execute(ADD, (url, title, utc))
        db.execute(PRUNE)


class VisitSqlTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.version = initialize_model(self.db, 0)

    def tearDown(self):
        self.db.close()

    def test_fresh_schema_and_settings_constraints_survive_v2(self):
        self.assertEqual(self.version, 2)
        self.assertEqual(self.db.execute(SETTINGS_READ).fetchall(), [(1, "positron://newtab")])
        self.assertEqual(self.db.execute(READ, (0,)).fetchall(), [])
        for sql in ("UPDATE app_settings SET schema_version=2",
                    "INSERT INTO app_settings VALUES(2,1,'positron://newtab')",
                    "UPDATE app_settings SET startup_page='https://example.com'"):
            with self.assertRaises(sqlite3.IntegrityError):
                self.db.execute(sql)
            self.db.rollback()
        self.assertEqual([row[1:3] for row in self.db.execute("PRAGMA table_info(app_visits)")],
                         [("id", "INTEGER"), ("url", "TEXT"), ("title", "TEXT"),
                          ("visited_utc", "INTEGER")])

    def test_v1_preferences_preserved_for_each_choice(self):
        for page in ("positron://newtab", "positron://welcome", "positron://controls"):
            with closing(sqlite3.connect(":memory:")) as db:
                self.assertEqual(apply_migration_model(db, 0, 1, SETTINGS_SCHEMA), 1)
                db.execute(SETTINGS_WRITE, (page,))
                db.commit()
                self.assertEqual(initialize_model(db, 1), 2)
                self.assertEqual(db.execute(SETTINGS_READ).fetchall(), [(1, page)])

    def test_v2_file_reopen_and_cursor_after_500_cap(self):
        with tempfile.TemporaryDirectory(prefix="app-visits-offline-") as directory:
            path = Path(directory) / "访问.db"
            with closing(sqlite3.connect(path)) as db:
                version = initialize_model(db, 0)
                db.execute(SETTINGS_WRITE, ("positron://controls",))
                db.commit()
                for index in range(521):
                    add_visit(db, "https://example.com/页面/" + str(index),
                              "标题 " + str(index), 521 - index)
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(initialize_model(db, version), 2)
                self.assertEqual(db.execute(SETTINGS_READ).fetchone(), (1, "positron://controls"))
                self.assertEqual(db.execute("SELECT count(*),min(id),max(id) FROM app_visits").fetchone(),
                                 (500, 22, 521))
                cursor = 0
                seen = []
                while True:
                    rows = db.execute(READ, (cursor,)).fetchall()
                    page = rows[:16]
                    self.assertLessEqual(len(rows), 17)
                    if not page:
                        break
                    seen.extend(row[0] for row in page)
                    cursor = page[-1][0]
                    if len(rows) <= 16:
                        break
                self.assertEqual(seen, list(range(521, 21, -1)))
                self.assertEqual(db.execute(READ, (22,)).fetchall(), [])

    def test_exact_page_and_lookahead_sizes(self):
        for count in (0, 1, 16, 17, 32, 33):
            with self.subTest(count=count):
                self.db.execute(CLEAR)
                self.db.commit()
                for index in range(count):
                    add_visit(self.db, "https://example.com/", "", index)
                rows = self.db.execute(READ, (0,)).fetchall()
                self.assertEqual(len(rows), min(count, 17))
                self.assertEqual(len(rows) > 16, count > 16)

    def test_duplicate_urls_remain_separate_visits_and_parameters_are_data(self):
        url = "https://example.com/';DELETE FROM app_settings;--"
        for _ in range(2):
            add_visit(self.db, url, "标题' --", 2147483648)
        rows = self.db.execute(READ, (0,)).fetchall()
        self.assertEqual([row[2:] for row in rows], [(url, "标题' --")] * 2)
        self.assertGreater(rows[0][0], rows[1][0])
        self.assertEqual([row[1] for row in rows], [2147483648] * 2)
        self.assertEqual(self.db.execute(SETTINGS_EXISTS).fetchone(), (1,))

    def test_prune_failure_rolls_back_insert_and_deletions(self):
        for index in range(500):
            add_visit(self.db, "https://example.com/" + str(index), "title", index)
        before = self.db.execute(READ, (0,)).fetchall()
        self.db.executescript("CREATE TRIGGER fail_prune BEFORE DELETE ON app_visits "
                             "BEGIN SELECT RAISE(ABORT,'offline prune failure'); END;")
        with self.assertRaises(sqlite3.IntegrityError):
            add_visit(self.db, "https://example.com/new", "new", 501)
        self.assertEqual(self.db.execute("SELECT count(*),min(id),max(id) FROM app_visits").fetchone(),
                         (500, 1, 500))
        self.assertEqual(self.db.execute(READ, (0,)).fetchall(), before)
        self.db.execute("DROP TRIGGER fail_prune")
        self.db.commit()
        add_visit(self.db, "https://example.com/new", "new", 501)
        self.assertEqual(self.db.execute("SELECT count(*),min(id),max(id) FROM app_visits").fetchone(),
                         (500, 2, 501))

    def test_clear_atomic_and_settings_untouched_and_ids_not_reused(self):
        self.db.execute(SETTINGS_WRITE, ("positron://welcome",))
        self.db.commit()
        for index in range(3):
            add_visit(self.db, "https://example.com/", "", index)
        self.db.execute(CLEAR)
        self.db.rollback()
        self.assertEqual(len(self.db.execute(READ, (0,)).fetchall()), 3)
        with self.db:
            self.db.execute(CLEAR)
        self.assertEqual(self.db.execute(READ, (0,)).fetchall(), [])
        self.assertEqual(self.db.execute(SETTINGS_READ).fetchone(), (1, "positron://welcome"))
        add_visit(self.db, "https://example.com/", "", 4)
        self.assertEqual(self.db.execute(READ, (0,)).fetchone()[0], 4)

    def test_clear_statement_failure_keeps_all_rows(self):
        for index in range(3):
            add_visit(self.db, "https://example.com/", "", index)
        self.db.executescript("CREATE TRIGGER fail_clear BEFORE DELETE ON app_visits "
                             "WHEN OLD.id=2 BEGIN SELECT RAISE(ABORT,'offline clear failure'); END;")
        with self.assertRaises(sqlite3.IntegrityError), self.db:
            self.db.execute(CLEAR)
        self.assertEqual(len(self.db.execute(READ, (0,)).fetchall()), 3)

    def test_migration_failure_rolls_back_new_tables(self):
        with closing(sqlite3.connect(":memory:")) as db:
            with self.assertRaises(sqlite3.IntegrityError):
                apply_migration_model(db, 0, 2, SETTINGS_SCHEMA + VISITS_SCHEMA +
                                      "INSERT INTO app_settings VALUES(2,1,'positron://newtab');")
            self.assertEqual(db.execute(SETTINGS_EXISTS).fetchone(), (0,))
            self.assertEqual(db.execute(VISITS_EXISTS).fetchone(), (0,))

    def test_v1_failed_migration_preserves_preference(self):
        with closing(sqlite3.connect(":memory:")) as db:
            apply_migration_model(db, 0, 1, SETTINGS_SCHEMA)
            db.execute(SETTINGS_WRITE, ("positron://welcome",))
            db.commit()
            with self.assertRaises(sqlite3.OperationalError):
                apply_migration_model(db, 1, 2, VISITS_SCHEMA + "INVALID SQL;")
            self.assertEqual(db.execute(SETTINGS_READ).fetchone(), (1, "positron://welcome"))
            self.assertEqual(db.execute(VISITS_EXISTS).fetchone(), (0,))

    def test_future_version_and_missing_v2_table_fail_closed(self):
        add_visit(self.db, "https://example.com/", "title", 1)
        before = self.db.execute(READ, (0,)).fetchall()
        with self.assertRaises(ValueError):
            initialize_model(self.db, 3)
        self.assertEqual(self.db.execute(READ, (0,)).fetchall(), before)
        self.db.execute("DROP TABLE app_visits")
        self.db.commit()
        with self.assertRaises(ValueError):
            initialize_model(self.db, 2)
        self.assertEqual(self.db.execute(VISITS_EXISTS).fetchone(), (0,))

    def test_bad_settings_not_repaired_by_migration(self):
        self.db.execute("DELETE FROM app_settings")
        self.db.commit()
        with self.assertRaises(ValueError):
            initialize_model(self.db, 2)
        self.assertEqual(self.db.execute(SETTINGS_READ).fetchall(), [])


class SourceContractTests(unittest.TestCase):
    """Source guards only; not execution evidence for C ownership/validation."""
    def test_header_and_heap_snapshot_contract(self):
        for name, value in (("APP_VISITS_PAGE_MAX", 16), ("APP_VISITS_URL_MAX", 2048),
                            ("APP_VISITS_TITLE_MAX", 256)):
            self.assertRegex(HEADER, rf"#define {name} {value}\b")
        for name, value in (("APP_VISITS_ADD", 3), ("APP_VISITS_LOAD", 4), ("APP_VISITS_CLEAR", 5)):
            self.assertRegex(HEADER, rf"{name} = {value}\b")
        self.assertIn("AppVisitSnapshot *visits;", HEADER)
        self.assertNotRegex(SOURCE, r"AppVisitSnapshot\s+\w+\s*[;\[]")
        worker = SOURCE[SOURCE.index("static DWORD WINAPI app_settings_worker"):]
        worker = worker[:worker.index("static int app_settings_owner")]
        self.assertIn("AppSettingsJob *job;", worker)
        self.assertNotRegex(worker, r"AppSettingsJob\s+\w+\s*;")
        self.assertIn("calloc(1, sizeof(*snapshot))", SOURCE)
        self.assertIn("else free(snapshot);", SOURCE)
        self.assertIn("AppSettingsResult_Release(&result);", SOURCE)
        self.assertIn("AppSettingsResult_Release(&store->results[index]);", SOURCE)
        self.assertIn("store->results[store->result_head].visits = NULL;", SOURCE)

    def test_single_public_migration_target_no_private_metadata(self):
        self.assertEqual(SOURCE.count("api->migration(db,"), 1)
        self.assertIn("api->migration(db, 2, script)", SOURCE)
        self.assertNotRegex(SOURCE, r'"[^"\n]*__(?:pdb|sqlite)')
        self.assertNotIn("sqlite3_", SOURCE)
        self.assertIn('TEXT("PDb_BindInt64")', SOURCE)

    def test_strict_row_checks_and_shared_admission(self):
        self.assertIn("bytes >= capacity", SOURCE)
        self.assertIn("!app_visits_utf8(text, bytes)", SOURCE)
        self.assertIn("code >= 0xd800 && code <= 0xdfff", SOURCE)
        self.assertIn("code < minimum || code > 0x10ffff", SOURCE)
        self.assertIn("api->column_type(statement, 1) != PDB_VALUE_INTEGER", SOURCE)
        self.assertIn("store->outstanding == APP_SETTINGS_QUEUE_MAX", SOURCE)
        self.assertEqual(SOURCE.count("store->outstanding++;"), 1)
        self.assertEqual(SOURCE.count("store->outstanding--;"), 1)
        self.assertIn("stop = store->closing && !found;", SOURCE)


if __name__ == "__main__":
    print("OFFLINE SQLite SQL/model/source checks only; NOT WM6 or C-worker evidence.")
    unittest.main()
