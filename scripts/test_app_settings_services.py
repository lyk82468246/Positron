"""Read-only EXE adapter contract checks, not C/WM6 execution evidence.

The standalone Debug consumer fixture still needs formal project wiring,
builds and a device run. No compiler, device or build output is touched here.
"""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "positron_app/app_settings_services.c").read_text(encoding="utf-8")
HEADER = (ROOT / "positron_app/app_settings_services.h").read_text(encoding="utf-8")
FIXTURE = (ROOT / "positron_app/app_settings_services_debug.c").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"\b" + re.escape(name) + r"\([^;]*?\)\s*\{", SOURCE)
    if not match:
        raise AssertionError(f"Function not found: {name}")
    start = match.end()
    # These adapter bodies contain no braces in comments or JSON except the
    # completion formatter; the next column-zero brace ends the function.
    end = SOURCE.index("\n}", start)
    return SOURCE[start:end]


class SettingsServicesBoundaryTests(unittest.TestCase):
    def test_every_public_dependency_declared(self):
        declarations = "\n".join((ROOT / path).read_text(encoding="utf-8")
                                 for path in (
                                     "positron_browser/positron_browser.h",
                                     "positron_json/positron_json.h",
                                     "positron_app/app_settings_store.h"))
        calls = set(re.findall(r"\b((?:PBrowser_|PJson_|AppSettingsStore_)\w+)\(", SOURCE))
        for call in calls:
            self.assertRegex(declarations, r"\b" + call + r"\(")
        self.assertNotIn("p_browser_services.h", SOURCE + HEADER)

    def test_no_sql_io_navigation_or_secondary_parser(self):
        for forbidden in ("PDb_", "sqlite3_", "CreateFile", "LoadLibrary",
                          "Sleep(", "WaitFor", "PHttp_", "PCore_", "strstr(",
                          "ScriptSessionEvaluate", "ScriptSessionDestroy"):
            self.assertNotIn(forbidden, SOURCE)
        self.assertIn("PJson_Parse(input)", SOURCE)
        self.assertIn("PJson_FreeString(canonical)", SOURCE)
        self.assertIn("PJson_Free(root)", SOURCE)

    def test_exact_two_method_whitelist(self):
        methods = SOURCE[SOURCE.index("g_settings_methods[]"):
                         SOURCE.index("static int app_settings_services_owner")]
        self.assertEqual(re.findall(r'"([a-z.]+)"', methods),
                         ["settings.read", "settings.write"])

    def test_registration_uses_identity_not_address(self):
        body = function("AppSettingsServices_Register")
        self.assertIn("identity != APP_SETTINGS_PAGE_EMBEDDED_SETTINGS", body)
        self.assertNotRegex(body, r"(?i)url|origin|scheme|redirect")
        self.assertIn("services->bound", body)
        self.assertIn("options.pw = services", body)
        self.assertIn("AFTER bootstrap COMPLETE", HEADER)

    def test_params_are_bounded_and_preserve_string_identity(self):
        body = function("app_settings_services_params")
        self.assertIn("request->params_bytes > APP_SETTINGS_SERVICES_PARAMS_MAX", body)
        self.assertLess(body.index("memcpy(input"), body.index("PJson_Parse(input)"))
        self.assertIn("PJson_GetObjectSize(root) == 1", body)
        self.assertIn('strcmp(key, "startupPage")', body)
        self.assertIn("AppSettingsStore_StartPageUrl", body)
        self.assertIn("strlen(canonical) == request->params_bytes", body)
        self.assertIn("memcmp(canonical, request->params_json", body)

    def test_submit_only_enqueues_copied_job_metadata(self):
        body = function("app_settings_services_submit")
        self.assertIn("AppSettingsStore_Submit", body)
        self.assertIn("request->tab_id != services->tab_id", body)
        self.assertIn("request->page_generation != services->generation", body)
        self.assertIn("pending[index].token = request->token", body)
        for forbidden in ("CompleteService", "PumpServices", "RevokeServices", "memcpy("):
            self.assertNotIn(forbidden, body)

    def test_completion_identity_and_operation_guard_precede_dll(self):
        body = function("AppSettingsServices_AcceptResult")
        complete = body.index("PBrowser_ScriptSessionCompleteService")
        for guard in ("!services->active", "result->tab_id != services->tab_id",
                      "result->generation != services->generation",
                      "store_request_id == result->request_id",
                      "pending->operation != result->operation"):
            self.assertLess(body.index(guard), complete)
        self.assertNotIn("PumpServices", body)
        self.assertNotIn("result->error.message", body)

    def test_owner_pump_attempts_one_delivery_and_fails_closed(self):
        body = function("AppSettingsServices_Pump")
        self.assertIn("app_settings_services_owner", body)
        self.assertIn("services->session, 1, out_delivered", body)
        self.assertIn("services->active = 0", body)

    def test_revoke_disables_before_dll_and_does_not_cancel_write(self):
        body = function("AppSettingsServices_Revoke")
        self.assertLess(body.index("services->active = 0"),
                        body.index("PBrowser_ScriptSessionRevokeServices"))
        self.assertIn("memset(services->pending", body)
        self.assertNotIn("AppSettingsStore_", body)
        self.assertNotIn("PDb_Cancel", SOURCE)
        self.assertIn("until its borrowed session is Destroyed", HEADER)

    def test_fixture_is_debug_only_and_not_runtime_activated(self):
        self.assertLess(FIXTURE.index("#ifdef _DEBUG"),
                        FIXTURE.index("int AppSettingsServices_DebugCheck"))
        self.assertIn('AppSettingsStore_Create(dll_path, ":memory:"', FIXTURE)
        self.assertNotIn("DeleteFile", FIXTURE)
        self.assertIn('"../positron_script/positron_script.h"', FIXTURE)
        main = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        project = (ROOT / "positron_app/positron_app.vcproj").read_text(encoding="utf-8")
        winmain = main[main.index("int WINAPI WinMain("):]
        self.assertRegex(winmain, re.compile(
            r"#ifdef _DEBUG.*?--selftest-settings-services.*?"
            r"AppSettingsServices_DebugCheck\(\).*?#endif\s*startup_has_reference = 0;", re.S))
        self.assertEqual(main.count("AppSettingsServices_DebugCheck()"), 1)
        self.assertNotIn("AppSettingsServices_Register", main)
        self.assertIn('RelativePath=".\\app_settings_services.c"', project)
        self.assertIn("positron_json.lib", project)


if __name__ == "__main__":
    unittest.main()
