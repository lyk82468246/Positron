"""Read-only FLV fixture oracle; desktop decoding is not WM6 acceptance."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, microseconds, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the pinned generator")
    cases = [("flv-h264-aac.flv", 320, 240, 21000, "IPP", "h264"),
             ("flv-main-vga.flv", 640, 480, 400000, "IBP", "h264"),
             ("flv-oversize.flv", 656, 480, 0, "IPP", "h264"),
             ("flv1-unsupported.flv", 320, 240, 0, "IPP", "flv1")]
    for name, width, height, start, kinds, codec in cases:
        raw, log = decode(opts.ffmpeg, name, ["-copyts", "-map", "0:v:0", "-vf", "showinfo",
                          "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt",
                          "yuv420p", "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        require(f"Video: {codec}" in log, f"{name}: codec")
        frames = re.findall(r"n:\s*(\d+)\s+pts:\s*\S+\s+pts_time:(\S+)\s+duration:\s*\S+"
                            r"\s+duration_time:(\S+)\s+fmt:(\S+).*?\bs:(\d+)x(\d+)"
                            r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)", log)
        require(len(frames) == 3 and len(raw) == width * height * 3 // 2 * 3,
                f"{name}: frame/byte count")
        offset = 0
        for index, frame in enumerate(frames):
            number, pts, duration, fmt, w, h, field, key, kind = frame
            require((int(number), int(w), int(h), fmt, field, int(key), kind) ==
                    (index, width, height, "yuv420p", "P", int(index == 0), kinds[index]),
                    f"{name}: layout/type")
            require(microseconds(pts) == start + index * 200000 and
                    microseconds(duration) == (0 if codec == "flv1" else 200000), f"{name}: timeline")
            for count, expected in [(width * height, 81), (width * height // 4, 90),
                                    (width * height // 4, 240)]:
                plane = raw[offset:offset + count]
                require(min(plane) >= expected - 1 and max(plane) <= expected + 1,
                        f"{name}: red pixels")
                offset += count
        print(f"ORACLE {name}: {width}x{height}, {kinds}, start={start}, I420=81/90/240")
    name = "flv-h264-aac.flv"
    pcm, log = decode(opts.ffmpeg, name, ["-copyts", "-map", "0:a:0", "-c:a", "pcm_s16le",
                      "-threads:a", "1", "-f", "s16le", "pipe:1"])
    require("Audio: aac (LC), 48000 Hz, stereo" in log and len(pcm) == 30720 * 4,
            "FLV AAC layout/sample count")
    data = (media_fixtures.DEST / name).read_bytes()
    require(data[:5] == b"FLV\x01\x05", "FLV signature/stream flags")
    position = int.from_bytes(data[5:9], "big") + 4
    audio_pts = []
    while position < len(data):
        size = int.from_bytes(data[position + 1:position + 4], "big")
        payload = data[position + 11:position + 11 + size]
        require(len(payload) == size and position + 15 + size <= len(data), "FLV tag bounds")
        require(int.from_bytes(data[position + 11 + size:position + 15 + size], "big") ==
                11 + size, "FLV previous-tag size")
        if data[position] == 8 and len(payload) >= 2 and payload[1] == 1:
            audio_pts.append(int.from_bytes(data[position + 4:position + 7], "big") |
                             (data[position + 7] << 24))
        position += 15 + size
    require(audio_pts == [0] + [21 + (index * 1024 + 24) // 48 for index in range(29)],
            "FLV AAC millisecond packet timestamps")
    require(any(pcm), "FLV AAC unexpectedly silent")
    print("ORACLE FLV AAC: 30x1024 stereo samples, millisecond packet PTS, priming retained")
    print("FLV desktop oracle PASS; WM6 rejection/seek require a separate device gate.")


if __name__ == "__main__":
    main()
