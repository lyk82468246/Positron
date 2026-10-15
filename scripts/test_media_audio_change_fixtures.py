"""Read-only ADTS format-change oracle; never proves WM6 scheduling support.

Verify whole-frame concatenation, LC header/sample counts and independently
decoded frame formats using the manifest's pinned desktop tool. PCM bytes are
not a cross-version/ARM decoder reference.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


CONTROLS = {
    "aac-lc.aac": (48000, 1, 30),
    "aac-lc-44100.aac": (44100, 1, 27),
    "aac-lc-stereo.aac": (48000, 2, 30),
}
SEGMENTS = {
    "repeat": (0, 0), "rate-down": (0, 1), "rate-up": (1, 0),
    "stereo": (0, 2), "mono": (2, 0), "rate-return": (0, 1, 0),
}


def adts_frames(data):
    frames = []
    position = 0
    rates = (96000, 88200, 64000, 48000, 44100, 32000, 24000,
             22050, 16000, 12000, 11025, 8000, 7350)
    while position < len(data):
        header = data[position:position + 7]
        require(len(header) == 7 and header[0] == 255 and header[1] == 0xf1,
                "Pinned MPEG-4 ADTS header, no CRC")
        size = ((header[3] & 3) << 11) | (header[4] << 3) | (header[5] >> 5)
        frequency = (header[2] >> 2) & 15
        require(header[2] >> 6 == 1 and frequency < len(rates) and header[6] & 3 == 0,
                "AAC-LC, known sampling index, one raw block per frame")
        require(size >= 7 and position + size <= len(data), "Complete ADTS frame")
        channels = ((header[2] & 1) << 2) | (header[3] >> 6)
        frames.append((rates[frequency], channels, 1024))
        position += size
    require(position == len(data), "Whole ADTS boundary")
    return frames


def decoded_frames(exe, name):
    unused, log = decode(exe, name, ["-map", "0:a:0", "-af", "ashowinfo",
                                   "-c:a", "pcm_s16le", "-threads:a", "1", "-f", "null", "-"])
    require("Audio: aac (LC)" in log, f"{name}: AAC-LC decoder")
    matches = re.findall(r"\bfmt:fltp\s+channels:(\d+).*?\brate:(\d+)\s+nb_samples:(\d+)", log)
    return [(int(rate), int(channels), int(samples)) for channels, rate, samples in matches]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    manifest = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == manifest["generator_sha256"],
            "Use the existing pinned generator")
    names = list(CONTROLS)
    for name, (rate, channels, count) in CONTROLS.items():
        expected = [(rate, channels, 1024)] * count
        data = (media_fixtures.DEST / name).read_bytes()
        require(adts_frames(data) == expected, f"{name}: wire frame formats/count")
        require(decoded_frames(opts.ffmpeg, name) == expected, f"{name}: decoded frame formats/count")
        print(f"ORACLE {name}: {count} LC frames, {rate} Hz, {channels} channels")
    for stem, segments in SEGMENTS.items():
        name = "aac-clock-" + stem + ".aac"
        expected = []
        for index in segments:
            rate, channels, count = CONTROLS[names[index]]
            expected += [(rate, channels, 1024)] * count
        data = (media_fixtures.DEST / name).read_bytes()
        require(data == b"".join((media_fixtures.DEST / names[i]).read_bytes() for i in segments),
                f"{name}: exact whole-segment bytes")
        require(adts_frames(data) == expected, f"{name}: wire formats/count")
        require(decoded_frames(opts.ffmpeg, name) == expected, f"{name}: independent decoded formats/count")
        print(f"ORACLE {name}: unchanged complete segments {segments}, {len(expected)} decoded frames")
    print("Audio change oracle PASS; PCM prefix fidelity, FORMAT and terminal states require WM6 gates.")


if __name__ == "__main__":
    main()
