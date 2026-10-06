"""Read-only startup wiring checks; the device gate checks actual activation."""
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
MAIN = (ROOT / 'positron_app/main.c').read_text(encoding='utf-8')
STARTUP = MAIN[MAIN.index('int WINAPI WinMain('):]


class StartupWindowTests(unittest.TestCase):
    def test_activate_once_before_native_fixtures_and_navigation(self):
        activate = STARTUP.index('SetForegroundWindow(hwnd)')
        self.assertLess(STARTUP.index('ShowWindow(hwnd,'), activate)
        self.assertLess(activate, STARTUP.index('app_tabs_debug_check()'))
        self.assertLess(activate, STARTUP.index('app_load_page_from(hwnd,'))
        self.assertEqual(MAIN.count('SetForegroundWindow('), 1)

    def test_final_focus_does_not_steal_foreground(self):
        self.assertIn('if (GetForegroundWindow() == hwnd)\n'
                      '        SetFocus(g_page_window', STARTUP)
        self.assertEqual(STARTUP.count('ShowWindow(hwnd,'), 1)

    def test_device_gate_requires_real_window_state(self):
        gate = (ROOT / 'scripts/app_history_gate.ps1').read_text(encoding='utf-8')
        self.assertIn('startup-window visible=1 foreground=1 page_visible=1', gate)
        self.assertIn('IsWindowVisible(hwnd)', STARTUP)
        self.assertIn('IsWindowVisible(g_page_window)', STARTUP)
        diagnostic = STARTUP.index('char startup_window_log[128]')
        self.assertGreater(STARTUP.rfind('#ifdef _DEBUG', 0, diagnostic),
                           STARTUP.index('app_settings_start(hwnd)'))


if __name__ == '__main__':
    unittest.main()
