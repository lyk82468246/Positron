"""Offline fixture/orchestration checks, NOT ARM/WM6 DB acceptance.

The host SQLite oracle checks the exact fixture SQL and UTF-8 byte expectations.
It neither compiles nor loads the Positron DLL and does not count as any of the
remaining cross-process, filesystem, lock, journal or fault-injection gates.
"""
import ast
from contextlib import closing
from pathlib import Path
import re
import sqlite3
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
from c89ize import transform


ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "device_tools/db_file_probe"
SOURCE = (PROBE / "db_file_probe.c").read_text(encoding="utf-8")
LOCK_SOURCE = (PROBE / "db_lock_probe.h").read_text(encoding="utf-8")
JOURNAL_SOURCE = (PROBE / "db_journal_probe.h").read_text(encoding="utf-8")
QUOTA_SOURCE = (PROBE / "db_quota_probe.h").read_text(encoding="utf-8")


def c_strings(text):
    return "".join(ast.literal_eval(part) for part in
                   re.findall(r'"(?:\\.|[^"\\])*"', text))


def c_bytes(name):
    match = re.search(r"static const char " + name + r"\[\] =\s*"
                      r'((?:"(?:\\.|[^"\\])*"\s*)+);', SOURCE)
    if not match:
        raise AssertionError(f"missing fixture constant {name}")
    return c_strings(match.group(1)).encode("latin1")


SCHEMA_LINE = re.search(r"#define PROBE_SCHEMA_SQL (.*?)(?=\n\n)",
                        SOURCE, re.S).group(1)
SCHEMA = c_strings(SCHEMA_LINE)
TEXT_A = c_bytes("g_committed_text")
TEXT_B = c_bytes("g_second_text")
BLOB = bytes((0, 1, 127, 128, 255, 0))


class ProbeBoundaryTests(unittest.TestCase):
    def test_readonly_c89_transform(self):
        transformed, changes = transform(SOURCE)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, SOURCE)
        transformed, changes = transform(LOCK_SOURCE)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, LOCK_SOURCE)
        transformed, changes = transform(JOURNAL_SOURCE)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, JOURNAL_SOURCE)
        transformed, changes = transform(QUOTA_SOURCE)
        self.assertEqual(changes, 0)
        self.assertEqual(transformed, QUOTA_SOURCE)

    def test_only_public_db_api(self):
        for forbidden in ("sqlite3_", "sqlite3.h", "__pdb_", "__sqlite",
                          "CreateThread(", "OpenProcess(", "DeleteFile",
                          "RemoveDirectory", "CreateFileMapping"):
            self.assertNotIn(forbidden, SOURCE)
            self.assertNotIn(forbidden, LOCK_SOURCE)
            self.assertNotIn(forbidden, JOURNAL_SOURCE)
            self.assertNotIn(forbidden, QUOTA_SOURCE)
        self.assertIn('#include "positron_db.h"', SOURCE)
        self.assertIn('PDb_ApplyMigration(db, 1, "")', SOURCE)
        self.assertIn('"PRAGMA integrity_check"', SOURCE)

    def test_coordinator_never_opens_a_db(self):
        coordinator = SOURCE[SOURCE.index("static BOOL probe_coordinator("):
                             SOURCE.index("int WINAPI WinMain(")]
        self.assertNotIn("PDb_", coordinator)
        roles = re.findall(r'probe_spawn\(executable, leaf, L"([\w-]+)"',
                           coordinator)
        self.assertEqual(roles, ["create", "read", "verify-b"])
        self.assertIn("GetExitCodeProcess(g_children[slot]", SOURCE)
        self.assertIn("WaitForSingleObject(g_children[slot], PROBE_CHILD_TIMEOUT_MS)",
                      SOURCE)
        self.assertIn("exit_code != 0", SOURCE)
        self.assertIn("g_children[slot] = process.hProcess", SOURCE)
        self.assertIn("creator_exited_before_reader=1", SOURCE)
        self.assertIn("reader_exited_before_verifier=1", SOURCE)

    def test_timeout_can_only_terminate_owned_createprocess_handle(self):
        spawn = SOURCE[SOURCE.index("static BOOL probe_launch("):
                       SOURCE.index("static BOOL probe_coordinator(")]
        self.assertEqual(spawn.count("TerminateProcess("), 1)
        timeout = spawn[spawn.index("if (waited == WAIT_TIMEOUT)"):]
        self.assertIn("TerminateProcess(g_children[slot],", timeout)
        self.assertIn("CreateProcessW(executable, command", spawn)
        self.assertIn("#define PROBE_CHILD_TIMEOUT_MS 60000UL", SOURCE)

    def test_unique_owned_fixture_and_preserved_evidence(self):
        self.assertIn('L"fixture-%lu-%lu"', SOURCE)
        self.assertIn("!CreateDirectoryW(root, NULL)", SOURCE)
        self.assertIn("probe_marker(root, TRUE)", SOURCE)
        self.assertIn("probe_marker(root, FALSE)", SOURCE)
        self.assertIn("CREATE_NEW", SOURCE)
        self.assertNotIn("CREATE_ALWAYS", SOURCE)
        self.assertIn('L"--run-unicode-internal"', SOURCE)
        self.assertIn('L"\\\\Storage Card\\\\Temp\\\\Positron-device-gate\\\\db-file-"',
                      SOURCE)
        self.assertIn('L"\\\\Temp\\\\Positron-device-gate\\\\db-file-fixtures"',
                      SOURCE)
        self.assertIn("PRAGMA max_page_count=512", SOURCE)
        self.assertIn('"file_identity_guard=CREATE_NEW\\r\\n"', SOURCE)
        self.assertNotIn("GetLastError() != ERROR_FILE_NOT_FOUND", SOURCE)

    def test_project_links_only_public_import_library(self):
        # VS2008's ASCII XML declares gb2312; Python's Expat byte parser does
        # not support that encoding. Decode first, as the repo audit does.
        xml = (PROBE / "db_file_probe.vcproj").read_text(encoding="ascii")
        xml = re.sub(r"^\s*<\?xml[^>]*\?>", "", xml, count=1)
        project = ET.fromstring(xml)
        files = [n.attrib["RelativePath"] for n in project.iter("File")]
        self.assertEqual(files, [".\\db_file_probe.c", ".\\db_lock_probe.h",
                                 ".\\db_journal_probe.h", ".\\db_quota_probe.h", ".\\README.md"])
        configurations = list(project.iter("Configuration"))
        self.assertEqual(len(configurations), 2)
        for configuration in configurations:
            self.assertIn("Windows Mobile 6 Professional SDK (ARMV4I)",
                          configuration.attrib["Name"])
            linker = next(n for n in configuration.iter("Tool")
                          if n.attrib.get("Name") == "VCLinkerTool")
            self.assertRegex(linker.attrib["AdditionalDependencies"],
                             r"^\.\.\\\.\.\\positron_db\\bin\\"
                             r"(?:Debug|Release)\\positron_db\.lib$")
            self.assertEqual(linker.attrib["StackReserveSize"], "65536")

    def test_formal_solution_and_stage_integration(self):
        solution = (ROOT / "Positron.sln").read_text(encoding="utf-8-sig")
        guid = "{D8C04756-1F83-4A30-9C18-F6E7A49F3120}"
        project = re.search(r'= "positron_db_file_probe",.*?EndProject\s',
                            solution, re.S).group(0)
        self.assertIn(r'device_tools\db_file_probe\db_file_probe.vcproj', project)
        self.assertIn("{B4C6D8E0-1234-4A56-9B78-0CDEF1234567}", project)
        for config in ("Debug", "Release"):
            for action in ("ActiveCfg", "Build.0"):
                self.assertIn(guid + "." + config +
                              "|Windows Mobile 6 Professional SDK (ARMV4I)." + action,
                              solution)
        stage = (ROOT / "scripts/stage.bat").read_text(encoding="ascii")
        self.assertIn(r'device_tools\db_file_probe\bin\%CFG%\positron_db_file_probe.exe',
                      stage)
        self.assertNotIn("D8C04756", (ROOT / "positron_cab/positron_cab.vddproj").read_text(
            encoding="ascii"))

    def test_unicode_constants_are_exact(self):
        self.assertEqual(TEXT_A.decode("utf-8"), "中文读写")
        self.assertEqual(TEXT_B.decode("utf-8"), "B: 进程提交")
        self.assertIn(r'L"\\\x4e2d\x6587\x76ee\x5f55"', SOURCE)
        self.assertIn(r'L"\\\x6570\x636e\x5e93.sqlite"', SOURCE)

    def test_lock_pairs_open_before_lock_and_wait_on_peer_evidence(self):
        self.assertIn('L"--run-locks-internal"', SOURCE)
        self.assertIn("#define PROBE_HANDSHAKE_MS 15000UL", LOCK_SOURCE)
        for marker in ("rw-writer-open", "rw-read-held", "rw-commit-busy",
                       "rw-read-released", "rw-commit-done", "ww-contender-open",
                       "ww-write-held", "ww-write-busy", "ww-write-released"):
            self.assertIn('L"' + marker + '"', LOCK_SOURCE)
        self.assertIn('PDb_Exec(db, "BEGIN")', LOCK_SOURCE)
        self.assertIn('probe_busy(db, PDb_Commit(db), "rw-commit", 1,', LOCK_SOURCE)
        self.assertIn('probe_busy(db, PDb_Begin(db), "ww-begin", 0,', LOCK_SOURCE)
        self.assertIn("error.sqlite_code != 5", LOCK_SOURCE)
        self.assertIn("state.statement_count == 0", LOCK_SOURCE)
        self.assertIn("CREATE_NEW", LOCK_SOURCE)
        self.assertNotIn("CREATE_ALWAYS", LOCK_SOURCE)

    def test_lock_pair_coordinator_only_orchestrates_owned_children(self):
        sequence = LOCK_SOURCE[LOCK_SOURCE.index("static BOOL probe_lock_sequence("):]
        self.assertNotIn("PDb_", sequence)
        for slot, role in enumerate(("create", "rw-reader", "rw-writer",
                                     "ww-owner", "ww-contender", "lock-verify")):
            self.assertIn(f'L"{role}", {slot}, &child_pid)', sequence)
        self.assertIn("cold_verifier_after_all_exits=1", sequence)
        self.assertIn("!g_child_waited[child]", SOURCE)

    def test_journal_actual_spill_and_owned_abnormal_exit(self):
        self.assertIn('L"--run-journal-internal"', SOURCE)
        sequence = JOURNAL_SOURCE[JOURNAL_SOURCE.index("static BOOL probe_journal_sequence("):]
        self.assertNotIn("PDb_", sequence)
        self.assertEqual(JOURNAL_SOURCE.count("TerminateProcess("), 1)
        self.assertIn("TerminateProcess(g_children[2], PROBE_JOURNAL_EXIT)", sequence)
        self.assertIn("WaitForSingleObject(g_children[2], 0) != WAIT_TIMEOUT", sequence)
        self.assertIn("exit_code != PROBE_JOURNAL_EXIT", sequence)
        self.assertLess(sequence.index('L"hot.sqlite-journal"'),
                        sequence.index('L"journal-recover"'))
        writer = JOURNAL_SOURCE[JOURNAL_SOURCE.index('wcscmp(role, L"journal-writer") == 0) {'):
                                JOURNAL_SOURCE.index('g_phase = "journal_recovery_exact_data"')]
        self.assertNotIn("PDb_Commit", writer)
        self.assertNotIn("PDb_Rollback", writer)
        self.assertIn('"PRAGMA cache_size=4"', writer)
        self.assertIn('"PRAGMA cache_spill=ON"', writer)
        self.assertIn('"PRAGMA max_page_count=128"', JOURNAL_SOURCE)
        self.assertIn('"PRAGMA synchronous", 2', JOURNAL_SOURCE)
        self.assertIn('PDb_ApplyMigration(db, 1, "") == PDB_STATE &&', JOURNAL_SOURCE)
        self.assertLess(JOURNAL_SOURCE.index('PDb_ApplyMigration(db, 1, "")'),
                        JOURNAL_SOURCE.index('PDb_ApplyMigration(db, 2, "")'))
        self.assertIn("CREATE_NEW", JOURNAL_SOURCE)
        self.assertNotIn("CREATE_ALWAYS", JOURNAL_SOURCE)


    def test_quota_file_error_before_cleanup_and_fresh_processes(self):
        sequence = QUOTA_SOURCE[QUOTA_SOURCE.index("static BOOL probe_quota_sequence("):]
        self.assertNotIn("PDb_", sequence)
        for slot, role in enumerate(("create", "quota-writer", "quota-reopen", "quota-verify")):
            self.assertIn(f'L"{role}", {slot}, &child_pid)', sequence)
        self.assertIn('L"--run-quota-internal"', SOURCE)
        self.assertIn('"PRAGMA max_page_count=32"', QUOTA_SOURCE)
        self.assertIn('"PRAGMA synchronous", 2', QUOTA_SOURCE)
        self.assertIn("error.sqlite_code == 13", QUOTA_SOURCE)
        self.assertIn("error.transaction_active == 0", QUOTA_SOURCE)
        self.assertIn("error.cleanup_attempted == 0", QUOTA_SOURCE)
        self.assertNotIn("PDb_Rollback(", QUOTA_SOURCE)
        self.assertNotIn("TerminateProcess(", QUOTA_SOURCE)
        self.assertNotIn(":memory:", QUOTA_SOURCE)
        self.assertIn('L"\\\\quota-native-truncate.bin"', QUOTA_SOURCE)
        self.assertIn("NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL", QUOTA_SOURCE)
        self.assertIn("success = SetEndOfFile(file)", QUOTA_SOURCE)
        full = QUOTA_SOURCE[QUOTA_SOURCE.index("static BOOL probe_quota_full("):
                            QUOTA_SOURCE.index("static BOOL probe_quota_child(")]
        self.assertLess(full.index("PDb_GetErrorInfo"), full.index("PDb_Finalize"))
        self.assertIn("state.statement_count == 1", full)
        self.assertIn("finalized == PDB_ERROR", full)


class FixtureSqlOracleTests(unittest.TestCase):
    def test_file_quota_full_oracle_not_wm6_or_volume_exhaustion(self):
        with tempfile.TemporaryDirectory(prefix="db-quota-oracle-") as temporary:
            path = Path(temporary) / "fixture.sqlite"
            with closing(sqlite3.connect(path)) as db:
                db.execute("PRAGMA page_size=4096")
                db.execute("PRAGMA journal_mode=DELETE")
                db.execute("PRAGMA synchronous=FULL")
                db.executescript(SCHEMA + ";CREATE TABLE quota_samples(id INTEGER PRIMARY KEY,payload BLOB NOT NULL)")
                db.execute("INSERT INTO samples VALUES(1,?,?)", (TEXT_A.decode("utf-8"), BLOB))
                db.commit()
                self.assertEqual(db.execute("PRAGMA max_page_count=32").fetchone(), (32,))
                db.execute("PRAGMA cache_size=4")
                db.execute("PRAGMA cache_spill=ON")
                db.execute("BEGIN IMMEDIATE")
                db.execute("UPDATE samples SET label='quota-uncommitted'")
                db.execute("INSERT INTO quota_samples VALUES(1,?)", (BLOB,))
                with self.assertRaises(sqlite3.OperationalError) as raised:
                    db.execute("INSERT INTO quota_samples VALUES(2,zeroblob(262144))")
                self.assertEqual(raised.exception.sqlite_errorcode, sqlite3.SQLITE_FULL)
                self.assertFalse(db.in_transaction)
                self.assertEqual(db.execute("SELECT count(*) FROM quota_samples").fetchone(), (0,))
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute("SELECT id,label,payload FROM samples").fetchall(),
                                 [(1, TEXT_A.decode("utf-8"), BLOB)])
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                self.assertEqual(db.execute("PRAGMA max_page_count=32").fetchone(), (32,))
                db.execute("INSERT INTO samples VALUES(8,'after-full-reopen',?)", (BLOB,))
                db.execute("INSERT INTO quota_samples VALUES(1,?)", (BLOB,))
                db.commit()
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute("SELECT payload FROM quota_samples").fetchall(), [(BLOB,)])
                self.assertEqual(db.execute("SELECT count(*) FROM samples").fetchone(), (2,))
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                self.assertLessEqual(db.execute("PRAGMA page_count").fetchone()[0], 32)
            self.assertLessEqual(path.stat().st_size, 131072)

    def test_hot_journal_oracle_not_wm6_or_power_loss(self):
        with tempfile.TemporaryDirectory(prefix="db-journal-oracle-") as temporary:
            path = Path(temporary) / "fixture.sqlite"
            with closing(sqlite3.connect(path)) as db:
                db.execute("PRAGMA page_size=4096")
                db.execute("PRAGMA journal_mode=DELETE")
                db.execute("CREATE TABLE journal_samples(id INTEGER PRIMARY KEY,payload BLOB)")
                db.executemany("INSERT INTO journal_samples VALUES(?,?)",
                               [(i, b"A" * 2048) for i in range(1, 65)])
                db.commit()
            baseline = path.read_bytes()
            # Fresh host oracle deliberately exits without connection cleanup.
            # This is not the ARM fixture or evidence for WinCE locks/VFS.
            script = """import sqlite3,os,sys
db=sqlite3.connect(sys.argv[1])
db.execute('PRAGMA synchronous=FULL')
db.execute('PRAGMA cache_size=4')
db.execute('PRAGMA cache_spill=ON')
db.execute('BEGIN IMMEDIATE')
db.execute('UPDATE journal_samples SET payload=zeroblob(2048)')
os._exit(13)
"""
            result = subprocess.run([sys.executable, "-c", script, str(path)],
                                    timeout=10, check=False)
            self.assertEqual(result.returncode, 13)
            journal = Path(str(path) + "-journal").read_bytes()
            self.assertEqual(journal[:8], bytes.fromhex("d9d505f920a163d7"))
            records, nonce, pages, sector, page_size = struct.unpack(">5I", journal[8:28])
            self.assertGreater(records, 0)
            self.assertEqual(page_size, 4096)
            self.assertEqual(len(baseline), pages * page_size)
            self.assertGreaterEqual(len(journal), sector + records * 4104)
            self.assertNotEqual(path.read_bytes(), baseline)
            with closing(sqlite3.connect(path)) as db:
                rows = db.execute("SELECT id,payload FROM journal_samples ORDER BY id").fetchall()
                self.assertEqual(rows, [(i, b"A" * 2048) for i in range(1, 65)])
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(), ("ok",))
                db.execute("INSERT INTO journal_samples VALUES(65,?)", (b"recovered",))
                db.commit()
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute("SELECT payload FROM journal_samples WHERE id=65").fetchone(),
                                 (b"recovered",))

    def test_file_lock_sql_oracle_not_wm6(self):
        # Independent connections verify the chosen SQLite transaction sequence,
        # not WinCE cross-process locking or the product DLL.
        with tempfile.TemporaryDirectory(prefix="db-lock-oracle-") as temporary:
            path = Path(temporary) / "fixture.sqlite"
            with closing(sqlite3.connect(path, timeout=0)) as a, \
                    closing(sqlite3.connect(path, timeout=0)) as b:
                a.executescript(SCHEMA)
                a.execute("INSERT INTO samples VALUES(1,?,?)",
                          (TEXT_A.decode("utf-8"), BLOB))
                a.commit()
                a.execute("BEGIN")
                self.assertEqual(a.execute("SELECT count(*) FROM samples").fetchone(), (1,))
                b.execute("BEGIN IMMEDIATE")
                b.execute("INSERT INTO samples VALUES(4,'rw-committed',?)", (BLOB,))
                with self.assertRaises(sqlite3.OperationalError):
                    b.commit()
                self.assertTrue(b.in_transaction)
                self.assertEqual(a.execute("SELECT count(*) FROM samples").fetchone(), (1,))
                a.commit()
                b.commit()
                self.assertEqual(a.execute("SELECT count(*) FROM samples").fetchone(), (2,))
                a.execute("BEGIN IMMEDIATE")
                with self.assertRaises(sqlite3.OperationalError):
                    b.execute("BEGIN IMMEDIATE")
                self.assertFalse(b.in_transaction)
                a.commit()
                b.execute("BEGIN IMMEDIATE")
                b.commit()
                self.assertEqual(a.execute("PRAGMA integrity_check").fetchone(), ("ok",))

    def test_exact_binding_rollback_reopen_and_migration_failure(self):
        # Host-side oracle only; the ARM fixture must use PDb_* and new EXEs.
        with tempfile.TemporaryDirectory(prefix="db-file-oracle-") as temporary:
            directory = Path(temporary) / "中文目录"
            directory.mkdir()
            path = directory / "数据库.sqlite"
            path.touch(exist_ok=False)
            with closing(sqlite3.connect(path)) as db:
                db.executescript(SCHEMA)
                db.execute("INSERT INTO samples VALUES(?,?,?)",
                           (1, TEXT_A.decode("utf-8"), BLOB))
                db.commit()
                db.execute("INSERT INTO samples VALUES(?,?,?)",
                           (2, "rollback-only", BLOB))
                db.rollback()
                with self.assertRaises(sqlite3.OperationalError):
                    db.executescript("BEGIN;"
                                     "ALTER TABLE samples ADD COLUMN extra TEXT;"
                                     "UPDATE samples SET label='bad-migration';"
                                     "SELECT FROM broken;COMMIT;")
                db.rollback()
            with closing(sqlite3.connect(path)) as db:
                rows = db.execute("SELECT id,label,payload FROM samples").fetchall()
                self.assertEqual(rows, [(1, TEXT_A.decode("utf-8"), BLOB)])
                with self.assertRaises(sqlite3.OperationalError):
                    db.execute("SELECT extra FROM samples")
                self.assertEqual(db.execute("PRAGMA integrity_check").fetchone(),
                                 ("ok",))
                db.execute("INSERT INTO samples VALUES(?,?,?)",
                           (3, TEXT_B.decode("utf-8"), BLOB))
                db.commit()
            with closing(sqlite3.connect(path)) as db:
                rows = db.execute("SELECT id,label,payload FROM samples ORDER BY id").fetchall()
                self.assertEqual(rows, [(1, TEXT_A.decode("utf-8"), BLOB),
                                        (3, TEXT_B.decode("utf-8"), BLOB)])


if __name__ == "__main__":
    unittest.main()
