"""Host wiring guards, not a replacement for the actual worker/device gate."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
OWNER = (ROOT / 'positron_app/app_runtime_app.c').read_text(encoding='utf-8')
BRIDGE = (ROOT / 'positron_app/app_runtime_services.c').read_text(encoding='utf-8')
SCRIPT = (ROOT / 'positron_app/app_script.c').read_text(encoding='utf-8')
MAIN = (ROOT / 'positron_app/main.c').read_text(encoding='utf-8')


class RuntimeServicesTests(unittest.TestCase):
    def test_native_worker_keeps_public_ownership(self):
        for api in ('PRuntime_DevelopmentStorageOpen', 'PRuntime_ServiceQueueCreate',
                    'PRuntime_ServiceQueueProcessOne', 'PRuntime_ServiceQueueClose'):
            self.assertIn(api, OWNER)
            self.assertNotIn(api, BRIDGE)
        for forbidden in ('PDb_', 'PJson_Parse', 'sqlite3_', 'TerminateThread'):
            self.assertNotIn(forbidden, OWNER)
            self.assertNotIn(forbidden, BRIDGE)

    def test_bridge_submits_copies_not_sql(self):
        submit = BRIDGE[BRIDGE.index('static int app_runtime_submit'):BRIDGE.index('static void app_runtime_cancel')]
        for field in ('tab_id', 'page_generation', 'bridge_id', 'request_id', 'params_json', 'params_bytes'):
            self.assertIn(field, submit)
        self.assertIn('AppRuntimePage_Submit', submit)
        self.assertNotIn('WaitFor', submit)
        self.assertNotIn('Evaluate', submit)
        self.assertIn('{ "storage.sql" }', BRIDGE)

    def test_prepare_after_bootstrap_and_before_author(self):
        self.assertIn('context->bootstrap_pending = 0;\n        return context->runtime_bound ?', SCRIPT)
        self.assertIn('AppRuntimeServices_Prepare', SCRIPT)
        self.assertLess(BRIDGE.index('PBrowser_ScriptSessionRegisterServices'),
                        BRIDGE.index('PBrowser_ScriptSessionEvaluate'))
        self.assertIn('AppScript_BindRuntime(request->script_candidate', MAIN)

    def test_explicit_native_grant_and_required_capability(self):
        start = MAIN[MAIN.index('static int app_start_runtime('):]
        self.assertLess(start.index('decision != IDYES'), start.index('AppRuntimeApp_GrantSql'))
        self.assertLess(start.index('AppRuntimeApp_GrantSql'), start.index('g_runtime_tab_id = g_app.tab_id'))
        self.assertIn('if (AppRuntimeApp_RequiresSql(request->runtime_app)) return -1;', MAIN)

    def test_revocation_precedes_alias_release(self):
        revoke = BRIDGE[BRIDGE.index('void AppRuntimeServices_Revoke'):]
        self.assertLess(revoke.index('PBrowser_ScriptSessionRevokeServices'),
                        revoke.index('AppRuntimePage_Release'))
        finish = MAIN[MAIN.index('static void app_tabs_finish_close(void)\n{'):]
        self.assertLess(finish.index('AppScript_RevokeRuntime'),
                        finish.index('AppRuntimeApp_RequestClose'))
        self.assertIn('AppRuntimePage_Cancel', BRIDGE)

    def test_results_are_checked_and_not_reexecuted(self):
        pump = BRIDGE[BRIDGE.index('int AppRuntimeServices_Pump'):BRIDGE.index('void AppRuntimeServices_Revoke')]
        self.assertIn('result->tab_id != services->tab_id', pump)
        self.assertIn('result->page_generation != services->generation', pump)
        self.assertIn('PBrowser_ScriptSessionCompleteService', pump)
        self.assertIn('PBrowser_ScriptSessionPumpServices', pump)
        self.assertNotIn('Submit', pump)


if __name__ == '__main__':
    unittest.main()
