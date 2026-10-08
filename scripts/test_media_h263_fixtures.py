"""Read-only independent H.263 fixture oracle; not a WM6 device test."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


CASES = {"h263-cif.avi": (352, 288), "h263p-vga.avi": (640, 480),
         "h263-oversize.avi": (704, 576)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the pinned generator")
    for name, (width, height) in CASES.items():
        raw, log = decode(opts.ffmpeg, name, ["-map", "0:v:0", "-vf", "showinfo",
                         "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt",
                         "yuv420p", "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        require("Video: h263" in log and "Audio:" not in log, f"{name}: codec/streams")
        frames = re.findall(r"n:\s*(\d+)\s+pts:\s*\S+\s+pts_time:(\S+)\s+duration:\s*\S+"
                            r"\s+duration_time:(\S+)\s+fmt:(\S+).*?\bs:(\d+)x(\d+)"
                            r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)", log)
        require(len(frames) == 3 and len(raw) == width * height * 3 // 2 * 3,
                f"{name}: frame/byte count")
        offset = 0
        for index, frame in enumerate(frames):
            number, pts, duration, fmt, w, h, field, key, kind = frame
            require((int(number), int(w), int(h), fmt, field, int(key), kind) ==
                    (index, width, height, "yuv420p", "P", int(index == 0), "I" if index == 0 else "P"),
                    f"{name}: layout/type")
            require(Decimal(pts) * 1000000 == index * 200000 and
                    Decimal(duration) * 1000000 == 200000, f"{name}: timeline")
            for count, expected in [(width * height, 81), (width * height // 4, 90),
                                    (width * height // 4, 240)]:
                plane = raw[offset:offset + count]
                require(min(plane) >= expected - 1 and max(plane) <= expected + 1,
                        f"{name}: red pixels")
                offset += count
        print(f"ORACLE {name}: {width}x{height}, IPP, PTS=0/200000/400000, I420=81/90/240")
    print("H.263 desktop oracle PASS; WM6 support requires the separate device gate.")


if __name__ == "__main__":
    main()
