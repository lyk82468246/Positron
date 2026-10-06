"""Ignored diagnostic snapshots are not repository build projects."""
import os
import unittest
from unittest.mock import patch

import audit_repo


class ProjectScopeTests(unittest.TestCase):
    def scan(self, visible):
        tree = [
            (audit_repo.ROOT, [], ["Positron.vcproj"]),
            (os.path.join(audit_repo.ROOT, "new_project"), [],
             ["new.vcproj"]),
            (os.path.join(audit_repo.ROOT, "tmp", "snapshot"), [],
             ["old.vcproj"]),
        ]
        with patch.object(audit_repo.os, "walk", return_value=tree):
            return [audit_repo.relpath(path)
                    for path in audit_repo.iter_projects(visible)]

    def test_visible_untracked_projects_are_still_audited(self):
        self.assertEqual(self.scan({"Positron.vcproj",
                                    "new_project/new.vcproj"}),
                         ["Positron.vcproj", "new_project/new.vcproj"])

    def test_empty_git_inventory_is_not_filesystem_fallback(self):
        self.assertEqual(self.scan(set()), [])

    def test_without_git_inventory_existence_audit_is_preserved(self):
        self.assertEqual(len(self.scan(None)), 3)


if __name__ == "__main__":
    unittest.main()
