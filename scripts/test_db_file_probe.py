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
import tempfile
import unittest
import xml.etree.ElementTree as ET
from c89ize import transform


ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / "device_tools/db_file_probe"
SOURCE = (PROBE / "db_file_probe.c").read_text(encoding="utf-8")
LOCK_SOURCE = (PROBE / "db_lock_probe.h").read_text(encoding="utf-8")


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

    def test_only_public_db_api(self):
        for forbidden in ("sqlite3_", "sqlite3.h", "__pdb_", "__sqlite",
                          "CreateThread(", "OpenProcess(", "DeleteFile",
                          "RemoveDirectory", "CreateFileMapping"):
            self.assertNotIn(forbidden, SOURCE)
            self.assertNotIn(forbidden, LOCK_SOURCE)
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
        self.assertEqual(files, [".\\db_file_probe.c", ".\\db_lock_probe.h", ".\\README.md"])
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


class FixtureSqlOracleTests(unittest.TestCase):
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
