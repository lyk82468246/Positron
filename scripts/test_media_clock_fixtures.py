"""Read-only clock fixture oracle: pinned bytes/payloads and desktop decode.

The desktop CLI may synthesize frame PTS. Its frames are a pixel/count oracle,
not the WM6 DLL's presentation-clock or unknown-PTS contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


def video_tags(data):
    require(data[:3] == b"FLV" and data[3] == 1, "FLV header")
    position = int.from_bytes(data[5:9], "big") + 4
    require(data[position - 4:position] == b"\x00" * 4, "Initial previous-tag size")
    tags = []
    while position < len(data):
        require(position + 15 <= len(data), "Tag header bounds")
        size = int.from_bytes(data[position + 1:position + 4], "big")
        tag = data[position:position + 15 + size]
        require(len(tag) == size + 15 and int.from_bytes(tag[-4:], "big") == size + 11,
                "Tag length/previous-tag size")
        if tag[0] == 9:
            require(size >= 5 and tag[11] & 15 == 7, "AVC video tag")
            tags.append(tag)
        position += size + 15
    require(position == len(data), "Exact tag boundary")
    return tags


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the existing pinned generator")
    original = video_tags((media_fixtures.DEST / "flv-h264-aac.flv").read_bytes())
    cases = {
        "base": ([21, 221, 421], [0, 0, 0]),
        "unknown": ([21, 221, 421], [900001, 900001, 900001]),
        "late-unknown": ([21, 221, 421], [0, 900001, 900001]),
        "backward": ([21, 0, 421], [0, 0, 0]),
        "negative": ([21, 221, 421], [-22, 0, 0]),
        "gap": ([21, 621, 821], [0, 0, 0]),
        "duplicate": ([21, 21, 421], [0, 0, 0]),
    }
    for stem, (expected_dts, expected_cts) in cases.items():
        name = "flv-clock-" + stem + ".flv"
        data = (media_fixtures.DEST / name).read_bytes()
        require(data[:5] == b"FLV\x01\x01", f"{name}: video-only header")
        tags = video_tags(data)
        require(len(tags) == len(original) and len(data) == 13 + sum(map(len, tags)),
                f"{name}: only video tags, no audio/metadata")
        dts, cts = [], []
        for tag, source in zip(tags, original):
            mutable = bytearray(tag)
            if tag[12] == 1:
                dts.append(int.from_bytes(tag[4:7], "big") | (tag[7] << 24))
                composition = int.from_bytes(tag[13:16], "big")
                cts.append(composition - 0x1000000 if composition & 0x800000 else composition)
                mutable[4:8] = source[4:8]
                mutable[13:16] = source[13:16]
            require(bytes(mutable) == source, f"{name}: changed AVCC/header/payload beyond timestamps")
        require((dts, cts) == (expected_dts, expected_cts), f"{name}: exact wire DTS/CTS")
        raw, log = decode(opts.ffmpeg, name, ["-copyts", "-map", "0:v:0", "-vf", "showinfo",
                          "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt",
                          "yuv420p", "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        require("Video: h264 (Constrained Baseline)" in log and len(raw) == 320 * 240 * 3 // 2 * 3,
                f"{name}: codec/three decoded frames")
        require(len(re.findall(r"\bn:\s*\d+.*?\bfmt:yuv420p.*?\bs:320x240\s+i:P", log)) == 3,
                f"{name}: progressive I420")
        offset = 0
        for unused in range(3):
            for count, value in [(320 * 240, 81), (320 * 240 // 4, 90), (320 * 240 // 4, 240)]:
                plane = raw[offset:offset + count]
                require(min(plane) >= value - 1 and max(plane) <= value + 1, f"{name}: red pixels")
                offset += count
        print(f"ORACLE {name}: unchanged AVC payload, wire DTS={dts} CTS={cts}, three red I420 frames")
    print("Clock fixture oracle PASS; WM6 unknown/inferred PTS and scheduling require the device gate.")


if __name__ == "__main__":
    main()
