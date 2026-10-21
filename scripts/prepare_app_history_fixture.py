"""Prepare an isolated manual-review DB from a CLOSED device fixture DB.

Never edits the input or DLL metadata. This is host fixture preparation,
not evidence of C-worker execution, device durability or visual correctness.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path
import re
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
LOWER_ASCII = str.maketrans("ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz")


def records():
    rows = []

    def add(group, title, url=None):
        number = len(rows) + 1
        rows.append((url or f"https://example.com/history-fixture/{group}/{number:03}",
                     title, 1700000000 + number))

    for i in range(40):
        add("alpha", f"[Fixture] Alpha article {i + 1:02}")
    for i in range(17):
        add("chinese", f"[Fixture] \u4e2d\u6587\u6761\u76ee {i + 1:02}")
    for i in range(3):
        add("duplicate", f"[Fixture] Duplicate visit {i + 1}",
            "https://www.iana.org/help/example-domains")
    for title in ("Literal percent 100%", "Literal underscore a_b", "Single quote O'Reilly",
                  'Double quote "quoted"', '<b>Not markup</b> & "escaped"',
                  "SQL text: ' OR 1=1 --", "Backslash a\\b", "MiXeD CaSe Token"):
        add("special", "[Fixture] " + title)
    add("wrap", "[Fixture] Long URL", "https://example.com/history-fixture/wrap/" + "x" * 850)
    add("wrap", "[Fixture] Long title " + "T" * 210)
    add("wrap", "[Fixture] " + "continuousword" * 15)
    add("wrap", "[Fixture] " + "\u957f\u6807\u9898" * 20)
    add("edge", "")
    add("edge", "[Fixture] Latin \u00c4 exact")
    add("edge", "[Fixture] Latin \u00e4 exact")
    add("edge", "[Fixture] Boundary " + "Z" * 128)
    for i in range(4):
        add("other", f"[Fixture] Unrelated {i + 1}")
    assert len(rows) == 80
    assert all(len(url.encode("utf-8")) < 1024 and len(title.encode("utf-8")) < 256
               for url, title, _ in rows)
    return rows


def query_sql():
    source = (ROOT / "positron_app/app_settings_store.c").read_text(encoding="utf-8")
    match = re.search(r"static const char g_visits_search_sql\[\] =\s*"
                      r'((?:"(?:\\.|[^"\\])*"\s*)+);', source)
    if match is None:
        raise ValueError("Missing production search SQL")
    return "".join(ast.literal_eval(s) for s in re.findall(r'"(?:\\.|[^"\\])*"', match[1]))


def verify(db):
    if db.execute("PRAGMA integrity_check").fetchall() != [("ok",)]:
        raise ValueError("Fixture integrity failure")
    actual = db.execute("SELECT id,url,title,visited_utc FROM app_visits ORDER BY id").fetchall()
    if [r[1:] for r in actual] != records():
        raise ValueError("Fixture row mismatch")
    cases = []
    for query in ("", "alpha", "ALPHA", "\u4e2d\u6587", "Duplicate", "%", "_", "O'Reilly",
                  '"quoted"', "<b>", "' OR 1=1 --", "mixed case", "\u00c4", "\u00e4",
                  "Z" * 128, "no-such-fixture-result"):
        needle = query.translate(LOWER_ASCII)
        expected = [r[0] for r in reversed(actual)
                    if needle in r[1].translate(LOWER_ASCII) or needle in r[2].translate(LOWER_ASCII)]
        seen = []
        pages = []
        cursor = 0
        while True:
            result = db.execute(query_sql(), (cursor, query)).fetchall()
            page = result[:16]
            seen.extend(r[0] for r in page)
            pages.append(len(page))
            if len(result) <= 16:
                break
            cursor = page[-1][0]
        if seen != expected:
            raise ValueError("Filtered cursor mismatch: " + repr(query))
        cases.append(dict(query=query, count=len(expected), pages=pages, ids=expected))
    return dict(total=80, cases=cases, rejected_query="\u4e2d" * 50,
                rejected_utf8_bytes=150, purpose="isolated manual history review",
                note="Do not click fixture URLs except the explicit IANA duplicate case.")


def prepare(base, output):
    base = Path(base).resolve()
    output = Path(output).resolve()
    output.relative_to(ROOT / "tmp" / "device-runs")
    manifest = output.with_suffix(".json")
    if base == output or manifest.exists() or not base.is_file():
        raise ValueError("Require a closed input and fresh isolated output")
    if any(Path(str(base) + suffix).exists() for suffix in ("-journal", "-wal", "-shm")):
        raise ValueError("Input may not be closed")
    before = hashlib.sha256(base.read_bytes()).hexdigest()
    source = sqlite3.connect(base.as_uri() + "?mode=ro", uri=True)
    try:
        # Exclusive creation: never overwrite an existing fixture or user DB.
        with output.open("xb"):
            pass
        target = sqlite3.connect(output)
        try:
            source.backup(target)
            with target:
                target.execute("DELETE FROM app_visits")
                target.execute("DELETE FROM app_downloads")
                target.execute("UPDATE app_settings SET startup_page='positron://newtab' WHERE id=1")
                target.execute("UPDATE app_preferences SET startup_url='positron://newtab',"
                               "language=0,javascript_enabled=1 WHERE id=1")
                target.executemany("INSERT INTO app_visits(url,title,visited_utc) VALUES(?,?,?)", records())
            report = verify(target)
        finally:
            target.close()
    finally:
        source.close()
    if hashlib.sha256(base.read_bytes()).hexdigest() != before:
        raise ValueError("Input changed during preparation")
    report.update(base_sha256=before, database_sha256=hashlib.sha256(output.read_bytes()).hexdigest())
    with manifest.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, help="Closed device-created fixture DB")
    parser.add_argument("--output", required=True, help="Fresh DB under tmp/device-runs")
    args = parser.parse_args()
    result = prepare(args.base, args.output)
    print("Fixture prepared: rows=80; alpha=40 (16/16/8); Chinese=17 (16/1); duplicate=3")
    print("Host SQL/data checks only; visual review still required.")
