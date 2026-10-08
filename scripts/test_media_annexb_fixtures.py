"""Read-only pinned Annex-B oracle; CLI-generated PTS are not WM6 PTS."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(Path(opts.ffmpeg).read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the pinned generator")
    for stem, width, height, pixels, kinds, fields in [
        ("baseline-aac", 320, 240, "yuv420p", "IPP", "PPP"),
        ("main-vga", 640, 480, "yuv420p", "IBP", "PPP"),
        ("high", 320, 240, "yuv420p", "IBP", "PPP"),
        ("high422", 320, 240, "yuv422p", "IBP", "PPP"),
        ("interlaced", 320, 240, "yuv420p", "IBP", "BBB"),
        ("oversize", 656, 480, "yuv420p", "IPP", "PPP"),
    ]:
        name = "annexb-" + stem + ".h264"
        raw, log = decode(opts.ffmpeg, name, ["-debug_ts", "-map", "0:v:0", "-vf", "showinfo",
                          "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt", pixels,
                          "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        frames = re.findall(r"n:\s*\d+\s+pts:.*?fmt:(\w+).*?\bs:(\d+)x(\d+)"
                            r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)", log)
        require(len(frames) == 3, name + ": three decoded frames")
        require(log.count("demuxer ->") == 3 and
                all("pkt_pts:NOPTS" in line and "pkt_dts:NOPTS" in line
                    for line in log.splitlines() if "demuxer ->" in line), name + ": unknown packet PTS")
        plane_counts = [width * height, width * height // (2 if pixels == "yuv422p" else 4)]
        require(len(raw) == (plane_counts[0] + 2 * plane_counts[1]) * 3, name + ": bytes")
        offset = 0
        for index, frame in enumerate(frames):
            require(frame == (pixels, str(width), str(height), fields[index],
                              str(int(index == 0)), kinds[index]), name + ": frame properties")
            for count, target in [(plane_counts[0], 81), (plane_counts[1], 90), (plane_counts[1], 240)]:
                plane = raw[offset:offset + count]
                require(min(plane) >= target - 1 and max(plane) <= target + 1, name + ": pixels")
                offset += count
        data = (media_fixtures.DEST / name).read_bytes()
        units = re.split(b"\x00\x00\x00?\x01", data)[1:]
        require({7, 8, 5}.issubset({unit[0] & 31 for unit in units if unit}), name + ": SPS/PPS/IDR")
        print("ORACLE", name, width, height, pixels, kinds, "packet PTS/DTS unknown")
    print("Annex-B oracle PASS; CLI synthesized frame PTS do not define the DLL contract.")


if __name__ == "__main__":
    main()
