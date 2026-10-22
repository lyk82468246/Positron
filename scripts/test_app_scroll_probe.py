"""Source guardrails only; actual pixel/normal-exit evidence needs WM6."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ScrollProbeTests(unittest.TestCase):
    def test_scene_is_captured_before_cleanup(self):
        source = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        start = source.index("static int app_scroll_paint_debug_check(void)\n{")
        body = source[start:source.index("static int app_pointer_debug_check(void)\n{", start)]
        self.assertLess(body.index("WindowFromPoint(sample)"),
                        body.index("g_scroll_x = saved_x"))
        self.assertIn("actual != expected", body)
        self.assertIn("dpi <= 192; dpi += 96", body)
        self.assertIn("g_scroll_debug_paints - paints_before", body)

    def test_standalone_exits_before_storage(self):
        source = (ROOT / "positron_app/main.c").read_text(encoding="utf-8")
        start = source.index("if (startup_ui_selftest == 2)")
        end = source.index("if (g_downloads_ui_mode", start)
        body = source[start:end]
        self.assertIn("app_history_debug_check(1)", body)
        self.assertIn("AppHostContext_Shutdown", body)
        self.assertIn("AppDebug_EndSession", body)
        self.assertIn("return result", body)
        self.assertNotIn("AppSettingsStore", body)
        self.assertLess(start, source.index("AppHostContext_SetHttpInitialized", start))
        self.assertIn("app_history_debug_check(0)", source)

    def test_targeted_gate_does_not_claim_full_suite(self):
        source = (ROOT / "scripts/app_history_gate.ps1").read_text(encoding="utf-8")
        self.assertIn("'full_ui_selftests=NOT_RUN'", source)
        self.assertIn("Scroll fixture exit audit timed out", source)
        self.assertIn("New crash dump after scroll teardown", source)
        self.assertIn("'--selftest-scroll-paint'", source)
        self.assertIn("if ($text -match 'selftest FAILED') { throw", source)


if __name__ == "__main__":
    unittest.main()
