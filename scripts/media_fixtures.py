"""Offline procedural media fixtures; never run by the product build.

Use --generate --ffmpeg PATH only to intentionally replace the corpus/pin.
The default mode verifies committed byte counts and SHA-256 without FFmpeg.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "test_host" / "fixtures" / "media"
PIN = DEST / "manifest.json"


def run(exe, args):
    return subprocess.run([exe, "-hide_banner", "-loglevel", "error", "-nostdin"] + args,
                          check=True, stdout=subprocess.PIPE).stdout


def generate(exe):
    DEST.mkdir(parents=True, exist_ok=True)
    records = []
    cases = [
        ("baseline-aac.mp4", "320x240", "baseline", "yuv420p", "bframes=0", True),
        ("main-vga.mp4", "640x480", "main", "yuv420p", "bframes=2:8x8dct=0", False),
        ("high.mp4", "320x240", "high", "yuv420p", "8x8dct=1", False),
        ("high422.mp4", "320x240", "high422", "yuv422p", "8x8dct=1", False),
        ("interlaced.mp4", "320x240", "main", "yuv420p", "tff=1:8x8dct=0", False),
        ("oversize.mp4", "656x480", "baseline", "yuv420p", "bframes=0", False),
    ]
    for name, size, profile, pixels, params, audio in cases:
        args = ["-f", "lavfi", "-i", f"color=c=red:s={size}:r=5:d=0.6"]
        if audio:
            args += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6"]
        args += ["-map", "0:v", "-c:v", "libx264", "-threads:v", "1", "-preset", "medium",
                 "-profile:v", profile, "-pix_fmt", pixels, "-crf", "18",
                 "-x264-params", f"{params}:keyint=5:scenecut=0"]
        if audio:
            args += ["-map", "1:a", "-c:a", "aac", "-profile:a", "aac_low", "-ac", "2",
                     "-b:a", "64k", "-threads:a", "1"]
        else:
            args += ["-an"]
        args += ["-t", "0.6", "-fflags", "+bitexact", "-flags:v", "+bitexact",
                 "-map_metadata", "-1", "-movflags", "+faststart", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    for name, profile in [("aac-lc.aac", "aac_low"), ("aac-main.aac", "aac_main")]:
        args = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6",
                "-c:a", "aac", "-profile:a", profile, "-ac", "1", "-b:a", "64k",
                "-threads", "1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                "-f", "adts", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    for record in records:
        data = (DEST / record["file"]).read_bytes()
        record.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    version = subprocess.check_output([exe, "-version"], text=True).splitlines()[0]
    PIN.write_text(json.dumps({"origin": "procedural red/sine; no downloaded media",
                              "content_license": "CC0-1.0; no patent grant",
                              "generator_version": version,
                              "generator_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
                              "files": records}, indent=2) + "\n", encoding="utf-8")


def check():
    manifest = json.loads(PIN.read_text(encoding="utf-8"))
    for record in manifest["files"]:
        data = (DEST / record["file"]).read_bytes()
        assert len(data) == record["bytes"], record["file"]
        assert hashlib.sha256(data).hexdigest() == record["sha256"], record["file"]
        print(f"OK {record['file']}: {len(data)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--ffmpeg")
    opts = parser.parse_args()
    if opts.generate:
        if not opts.ffmpeg:
            parser.error("--generate requires an explicit --ffmpeg path")
        generate(opts.ffmpeg)
    check()
