"""Independently inspect the closed EXE installation registry and input digests."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sqlite3
import struct


def resource_digest(root):
    files = sorted((p.relative_to(root).as_posix().encode("utf-8"), p)
                   for p in root.rglob("*") if p.is_file())
    digest = hashlib.sha256(b"Positron.ResourceSet.v1\0" + struct.pack("<I", len(files)))
    for name, path in files:
        body = path.read_bytes()
        digest.update(struct.pack("<I", len(name)) + name)
        digest.update(struct.pack("<I", len(body)) + hashlib.sha256(body).digest())
    return digest.hexdigest()


def check(run):
    evidence = run / "app-runtime-install"
    expected = {
        "no-sql": (2, 2, 0), "schema-zero": (2, 2, 0),
        "sql": (2, 2, 1), "lost": (2, 2, 1),
        "unresolved": (2, 2, 1), "prefix": (2, 4, 2),
        "cancel": (4, 2, 1), "snapshot": (2, 2, 0),
    }
    uri = (evidence / "registry-cold.db").resolve().as_uri() + "?mode=ro"
    with sqlite3.connect(uri, uri=True) as db:
        db.row_factory = sqlite3.Row
        assert db.execute("PRAGMA integrity_check").fetchall()[0][0] == "ok"
        rows = list(db.execute("SELECT * FROM app_installs"))
    assert len(rows) == len(expected), (len(rows), len(expected))
    by_id = {r["app_id"]: r for r in rows}
    assert len(by_id) == len(rows)
    installations = set()
    for folder, terminal in expected.items():
        source = evidence / "inputs" / folder
        manifest = json.loads((source / "manifest.json").read_text(encoding="utf-8"))
        row = by_id[manifest["id"]]
        assert (row["state"], row["generation"], row["target_schema"]) == terminal, dict(row)
        assert row["target_schema"] == len(manifest["migrations"])
        assert row["revision"] == row["api_version"] == row["digest"] == row["approval_policy"] == 1
        assert row["app_version"] == manifest["version"]
        permissions = int("storage.sql" in manifest["permissions"])
        assert row["requested"] == row["approved"] == permissions
        assert re.fullmatch(r"[0-9a-f]{32}", row["installation_id"])
        assert int(row["installation_id"], 16) and row["installation_id"] not in installations
        installations.add(row["installation_id"])
        assert row["content_id"] == resource_digest(source), folder
        assert row["directory"].startswith("\\Temp\\Positron-device-gate\\app-runtime-install-")
        assert row["directory"].endswith("\\" + folder)
        for field, value in dict(row).items():
            assert type(value) is (str if field in {"installation_id", "app_id", "directory", "content_id"} else int)
    result = "status=PASS\nrecords=8\ncontent_digests=8/8\nstructured_fields=exact\nrecovery_generations=exact\n"
    (evidence / "registry-check.txt").write_text(result, encoding="utf-8")
    print(result, end="")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-root", required=True, type=Path)
    check(parser.parse_args().run_root)
