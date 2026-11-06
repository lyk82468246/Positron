"""Build a reproducible PXI ZIP from an author resource directory.

This is a PC packaging helper, not Runtime manifest or installation validation.
"""
import argparse
import os
import stat
from pathlib import Path
import tempfile
import zipfile


def build(source, output):
    source = source.resolve(strict=True)
    output = output.absolute()
    if not source.is_dir() or not (source / 'manifest.json').is_file():
        raise ValueError('Require a resource directory with root manifest.json')
    if output.resolve().is_relative_to(source):
        raise ValueError('Output must be outside the source directory')
    files = []
    total = 0
    names = set()
    for path in sorted(source.rglob('*')):
        if path.is_symlink() or (getattr(path.lstat(), "st_file_attributes", 0) &
                                 getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)):
            raise ValueError('Links and junctions are not package resources')
        if path.is_dir():
            continue
        if not path.is_file():
            raise ValueError('Only regular files are supported')
        name = path.relative_to(source).as_posix()
        key = name.casefold()
        if key in names:
            raise ValueError('Case-insensitive resource collision')
        names.add(key)
        if path.stat().st_size > 4 * 1024 * 1024 - total:
            raise ValueError("EXE resource budget exceeded")
        with path.open("rb") as stream:
            body = stream.read(4 * 1024 * 1024 - total + 1)
        total += len(body)
        if len(files) >= 128 or total > 4 * 1024 * 1024:
            raise ValueError('EXE package budget exceeded (128 files / 4 MiB)')
        files.append((name, body))
    # Final destination uses exclusive creation; a failed build never truncates it.
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, suffix='.pxi.tmp', delete=False) as stream:
            temporary = Path(stream.name)
            with zipfile.ZipFile(stream, 'w') as archive:
                for name, body in files:
                    info = zipfile.ZipInfo(name, (1980, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = 0o100644 << 16
                    archive.writestr(info, body)
        if temporary.stat().st_size > 16 * 1024 * 1024:
            raise ValueError('EXE input budget exceeded')
        # Publish the complete file without replacing an existing destination.
        if os.name == "nt":
            os.rename(temporary, output)  # Windows rename refuses an existing target.
        else:
            os.link(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return len(files), total


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    count, size = build(args.source, args.output)
    print('PXI built: %s (%d files, %d resource bytes); Runtime validation required' %
          (args.output, count, size))
