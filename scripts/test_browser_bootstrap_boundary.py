"""Audit every public ScriptSession entry against the private bootstrap gate."""
import pathlib
import re
import unittest


class BootstrapBoundaryTests(unittest.TestCase):
    def test_all_session_entries_use_gate_or_explicit_control_contract(self):
        path = pathlib.Path(__file__).resolve().parents[1] / "positron_browser" / "positron_browser.c"
        source = path.read_text(encoding="utf-8")
        source = re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                        " ", source, flags=re.S)
        bodies = {}
        pattern = r'PBROWSER_API[^;{}]*?\b(PBrowser_ScriptSession\w+)\s*\([^{};]*\)\s*\{'
        for match in re.finditer(pattern, source):
            depth, end = 1, match.end()
            while depth and end < len(source):
                depth += (source[end] == "{") - (source[end] == "}")
                end += 1
            self.assertEqual(depth, 0, match.group(1))
            bodies[match.group(1)] = source[match.end():end - 1]
        controls = {
            "PBrowser_ScriptSessionCreate", "PBrowser_ScriptSessionCreateEx",
            "PBrowser_ScriptSessionDestroy", "PBrowser_ScriptSessionBootstrapStep",
            "PBrowser_ScriptSessionBootstrapCancel", "PBrowser_ScriptSessionBootstrapGetState",
            "PBrowser_ScriptSessionGetBootstrapPerformanceInfo",
        }
        guarded = {name for name, body in bodies.items() if "p_script_session_valid(" in body}
        changed = True
        while changed:
            changed = False
            for name, body in bodies.items():
                if name not in guarded and name not in controls and any(
                        re.search(r'\b' + re.escape(target) + r'\s*\(', body) for target in guarded):
                    guarded.add(name)
                    changed = True
        self.assertGreater(len(bodies), 150)
        self.assertEqual(set(bodies) - guarded - controls, set())
        gate = source[source.index("static int p_script_session_valid("):]
        gate = gate[:gate.index("\n}")]
        for requirement in ("bootstrap_busy", "bootstrap_guarded", "PBROWSER_BOOTSTRAP_COMPLETE",
                            "GetCurrentThreadId"):
            self.assertIn(requirement, gate)
        for name in ("PBrowser_ScriptSessionBootstrapStep", "PBrowser_ScriptSessionBootstrapCancel",
                     "PBrowser_ScriptSessionBootstrapGetState"):
            self.assertIn("p_browser_bootstrap_idle(session)", bodies[name])
        self.assertIn("bootstrap_busy", bodies["PBrowser_ScriptSessionDestroy"])


if __name__ == "__main__":
    unittest.main()
