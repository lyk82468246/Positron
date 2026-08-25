#!/usr/bin/env python3
"""Write a ZIP archive whose every entry uses ZIP_STORED.

The Windows PowerShell available on older WM6 development machines can expose
System.IO.Compression but still ignore CompressionLevel.NoCompression.  This
small stdlib-only helper keeps the packaging contract explicit and portable.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import zipfile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True,
                        help="UTF-8 file with source<TAB>archive entries")
    parser.add_argument("--output", required=True,
                        help="output ZIP path")
    return parser.parse_args()


def load_entries(manifest_path: Path):
    entries = []
    for line_number, raw_line in enumerate(
            manifest_path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw_line.strip():
            continue
        if "\t" not in raw_line:
            raise ValueError(
                f"manifest line {line_number} is missing a tab separator")
        source, archive_name = raw_line.split("\t", 1)
        source_path = Path(source)
        if not source_path.is_file():
            raise FileNotFoundError(str(source_path))
        if not archive_name or archive_name.startswith("/"):
            raise ValueError(
                f"manifest line {line_number} has an invalid archive name")
        entries.append((source_path, archive_name.replace("\\", "/")))
    if not entries:
        raise ValueError("the ZIP manifest is empty")
    return entries


def main() -> int:
    args = parse_args()
    manifest_path = Path(args.manifest).resolve()
    output_path = Path(args.output).resolve()
    entries = load_entries(manifest_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()

    with zipfile.ZipFile(output_path, "w", compression=zipfile.ZIP_STORED,
                         allowZip64=True) as archive:
        for source_path, archive_name in entries:
            archive.write(source_path, archive_name, compress_type=zipfile.ZIP_STORED)

    with zipfile.ZipFile(output_path, "r") as archive:
        for info in archive.infolist():
            if info.compress_type != zipfile.ZIP_STORED:
                raise RuntimeError(
                    f"archive entry is not stored: {info.filename}")
    print(f"stored_entries={len(entries)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
