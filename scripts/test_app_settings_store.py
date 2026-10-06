"""Offline checks of the EXE's exact SQL and candidate activation boundary.

Does not compile, load WinCE DLLs, or claim to exercise the C worker. The
explicit Debug fixture and formal WM6 device gate remain required.
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


def c_string(name):
    # The semicolon inside a C literal is not the declaration terminator.
    match = re.search(r"static const char " + name + r"\[\] =\s*"
                      r'((?:"(?:\\.|[^"\\])*"\s*)+);', SOURCE)
    if not match:
        raise AssertionError(f"C SQL constant missing: {name}")
    return "".join(ast.literal_eval(part) for part in
                   re.findall(r'"(?:\\.|[^"\\])*"', match.group(1)))


SCHEMA = c_string("g_settings_schema")
READ = c_string("g_settings_read_sql")
WRITE = c_string("g_settings_write_sql")
EXISTS = c_string("g_settings_exists_sql")


class SettingsSqlTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(":memory:")
        self.db.executescript(SCHEMA)

    def tearDown(self):
        self.db.close()

    def test_default_and_app_table_probe(self):
        self.assertEqual(self.db.execute(EXISTS).fetchone(), (1,))
        self.assertEqual(self.db.execute(READ).fetchall(), [(1, "positron://newtab")])

    def test_all_choices_and_rollback(self):
        for page in ("positron://welcome", "positron://controls", "positron://newtab"):
            self.db.execute(WRITE, (page,))
            self.db.commit()
            self.assertEqual(self.db.execute(READ).fetchone(), (1, page))
        self.db.execute(WRITE, ("positron://controls",))
        self.db.rollback()
        self.assertEqual(self.db.execute(READ).fetchone(), (1, "positron://newtab"))

    def test_unregistered_or_command_values_rejected(self):
        for page in ("positron://quit", "http://example.com/", "", None,
                     "positron://newtab\x00suffix", "positron://welcome';DROP TABLE app_settings;--"):
            with self.assertRaises(sqlite3.IntegrityError):
                self.db.execute(WRITE, (page,))
            self.db.rollback()
            self.assertEqual(self.db.execute(READ).fetchone(), (1, "positron://newtab"))

    def test_schema_version_and_singleton_constraints(self):
        for sql in ("UPDATE app_settings SET schema_version=2",
                    "INSERT INTO app_settings VALUES(2,1,'positron://newtab')",
                    "INSERT INTO app_settings VALUES(1,1,'positron://newtab')"):
            with self.assertRaises(sqlite3.IntegrityError):
                self.db.execute(sql)
            self.db.rollback()
        self.assertEqual(self.db.execute(READ).fetchall(), [(1, "positron://newtab")])

    def test_missing_row_is_visible_not_silent_default(self):
        self.db.execute("DELETE FROM app_settings")
        self.db.execute(WRITE, ("positron://welcome",))
        self.assertEqual(self.db.execute(READ).fetchall(), [])

    def test_transactional_schema_failure_leaves_no_table(self):
        with closing(sqlite3.connect(":memory:")) as db:
            with self.assertRaises(sqlite3.IntegrityError):
                db.executescript("BEGIN;" + SCHEMA +
                                 "INSERT INTO app_settings VALUES(2,1,'positron://newtab');COMMIT;")
            db.rollback()
            self.assertEqual(db.execute(EXISTS).fetchone(), (0,))

    def test_unicode_file_close_reopen_on_host_only(self):
        with tempfile.TemporaryDirectory(prefix="app-settings-") as directory:
            path = Path(directory) / "设置.db"
            with closing(sqlite3.connect(path)) as db:
                db.executescript(SCHEMA)
                db.execute(WRITE, ("positron://controls",))
                db.commit()
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute(READ).fetchone(), (1, "positron://controls"))


class CandidateBoundaryTests(unittest.TestCase):
    def test_no_static_db_link_or_deployment_dependency(self):
        project = (ROOT / "positron_app/positron_app.vcproj").read_text(encoding="utf-8")
        self.assertNotIn("positron_db.lib", project)
        self.assertNotIn("positron_db.dll|", project)
        self.assertIn('RelativePath=".\\app_settings_store.c"', project)

    def test_isolated_debug_entry_precedes_runtime(self):
        main = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        # Must be in WinMain before normal argument parsing, not inside a
        # nested _DEBUG branch under an unrelated function's Release #else.
        winmain = main[main.index("int WINAPI WinMain("):]
        pattern = (r"#ifdef _DEBUG\s*int startup_ui_selftest;.*?"
                   r"#ifdef _DEBUG\s*startup_ui_selftest = 0;.*?"
                   r"/\* Explicit isolated adapter fixture.*?"
                   r"AppSettingsStore_DebugCheck\(\).*?#endif\s*"
                   r"startup_has_reference = 0;")
        self.assertRegex(winmain, re.compile(pattern, re.S))
        self.assertEqual(main.count("AppSettingsStore_DebugCheck()"), 1)
        self.assertEqual(main.count("AppSettingsStore_Create("), 1)
        self.assertNotIn("AppSettingsStore_Submit(", main)
        self.assertIn('L"positron.db"', main)
        self.assertIn("GetModuleFileNameW(NULL", main)
        self.assertIn("APP_SETTINGS_TIMER_ID", main)
        self.assertLess(winmain.index("app_tabs_debug_check()"),
                        winmain.index("app_settings_start(hwnd)"))

    def test_runtime_drains_before_window_destroy(self):
        main = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        close = main[main.index("static void app_tabs_close_all(HWND hwnd)\n{"):]
        self.assertLess(close.index("AppScript_RevokeSettings"),
                        close.index("AppSettingsStore_RequestClose"))
        finish = main[main.index("static void app_tabs_finish_close(void)\n{"):]
        self.assertLess(finish.index("if (g_settings_store != NULL) return;"),
                        finish.index("DestroyWindow(g_window)"))
        timer = main[main.index("    case WM_TIMER:"):]
        self.assertLess(timer.index("app_settings_tick(hwnd)"),
                        timer.index("if (g_tabs_closing) return 0;"))

    def test_async_startup_does_not_replace_new_user_navigation(self):
        main = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        tick = main[main.index("static void app_settings_tick(HWND hwnd)"):]
        for guard in ("g_settings_nav_serial == g_settings_startup_serial",
                      "g_app.tab_id == g_settings_startup_tab",
                      "!AppAddressBar_IsEditing(g_address_bar)",
                      "g_navigation_request == NULL", "APP_HISTORY_REPLACE"):
            self.assertIn(guard, tick)

    def test_initial_settings_refresh_uses_stable_url(self):
        main = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        winmain = main[main.index("int WINAPI WinMain("):]
        self.assertIn("app_load_local_page(hwnd, initial_url,", winmain)
        self.assertNotIn("app_load_local_page(hwnd, g_current_url,", winmain)

    def test_worker_uses_public_db_contract_not_private_sqlite(self):
        self.assertNotIn("sqlite3_", SOURCE)
        self.assertNotRegex(SOURCE, r'"[^"\n]*__(?:pdb|sqlite)')
        self.assertNotIn("TerminateThread(", SOURCE)
        self.assertNotIn("PDb_Cancel", SOURCE)
        self.assertNotIn("PostMessage", SOURCE)
        self.assertNotIn("DeleteFile", SOURCE)
        self.assertIn("PDb_GetConnectionState", SOURCE)
        self.assertIn("PDb_OpenUtf8Ex", SOURCE)

    def test_fixture_drains_full_queue_before_healthy_read(self):
        fixture = (ROOT / "positron_app/app_settings_store_debug.c").read_text(encoding="utf-8")
        quota = fixture.index("!= APP_SETTINGS_QUEUE_FULL")
        drain = fixture.index("result.request_id != requests[index]", quota)
        healthy_read = fixture.index("APP_SETTINGS_START_NEWTAB, 8, 10, &request)")
        self.assertLess(quota, drain)
        self.assertLess(drain, healthy_read)
        self.assertLess(healthy_read, fixture.index("phase = 4;"))


if __name__ == "__main__":
    unittest.main()
