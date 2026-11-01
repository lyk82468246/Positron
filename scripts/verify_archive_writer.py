"""Verify TEST1376's device-produced ZIP with Python's independent reader."""
import json
import sys
import zipfile
from pathlib import Path


def verify(path):
    expected = [
        ("stored.bin", 49157, zipfile.ZIP_STORED),
        ("text/", 0, zipfile.ZIP_STORED),
        ("text/中文.bin", 131089, zipfile.ZIP_DEFLATED),
        ("empty.txt", 0, zipfile.ZIP_STORED),
        ("tiny.bin", 3, zipfile.ZIP_STORED),
    ]
    if Path(path).stat().st_size > 262144:
        raise ValueError("fixture archive exceeds its sink budget")
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) != len(expected):
            raise ValueError("wrong entry count")
        for entry, (name, size, method) in zip(entries, expected):
            if (entry.filename, entry.file_size, entry.compress_type) != (name, size, method):
                raise ValueError("wrong name, size or method: " + name)
            if entry.date_time != (1980, 1, 1, 0, 0, 0):
                raise ValueError("expected valid fixed DOS timestamp: " + name)
            if entry.flag_bits & 0x809 != 0x808:
                raise ValueError("expected UTF-8/descriptor without encryption: " + name)
            reference = bytes((offset * 17 + (offset >> 8)) & 255 for offset in range(size))
            if archive.read(entry) != reference:
                raise ValueError("content or CRC mismatch: " + name)
        if archive.testzip() is not None:
            raise ValueError("CRC failure")
    return {"result": "PASS", "reader": "Python zipfile", "entries": len(expected),
            "bytes": Path(path).stat().st_size}


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2:
            raise ValueError("usage: verify_archive_writer.py <device ZIP>")
        print(json.dumps(verify(sys.argv[1]), ensure_ascii=True))
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as error:
        print(json.dumps({"result": "FAIL", "error": str(error)}, ensure_ascii=True))
        sys.exit(1)
