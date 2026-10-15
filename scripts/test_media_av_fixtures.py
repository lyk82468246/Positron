"""Read-only mixed MP4 oracle; WM6 scheduling requires separate device gates."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the pinned desktop generator")
    audio_reference = None
    for name, width, height, types in (("baseline-aac.mp4", 320, 240, "IPP"),
                                      ("mixed-main-aac.mp4", 640, 480, "IBP"),
                                      ("mixed-packet-limit.mp4", 320, 240, "IPP")):
        pixels, log = decode(opts.ffmpeg, name, ["-map", "0:v:0", "-vf", "showinfo",
                             "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt", "yuv420p",
                             "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        profile = "Main" if width == 640 else "Constrained Baseline"
        require(f"Video: h264 ({profile})" in log, f"{name}: pinned AVC profile")
        frames = re.findall(r"\bn:\s*(\d+)\s+pts:\s*\S+\s+pts_time:(\S+).*?"
                            r"\bfmt:yuv420p.*?\bs:(\d+)x(\d+)\s+i:P\s+iskey:\d\s+type:(\w)", log)
        require(len(frames) == 3 and len(pixels) == width * height * 3 // 2 * 3,
                f"{name}: three progressive I420 frames")
        frame_bytes = width * height * 3 // 2
        for i, (number, pts, w, h, kind) in enumerate(frames):
            require((int(number), float(pts), int(w), int(h), kind) ==
                    (i, i / 5, width, height, types[i]), f"{name}: frame layout/PTS/order")
            offset = i * frame_bytes
            for count, value in ((width * height, 81), (width * height // 4, 90),
                                 (width * height // 4, 240)):
                plane = pixels[offset:offset + count]
                require(min(plane) >= value - 1 and max(plane) <= value + 1,
                        f"{name}: all visible red pixels")
                offset += count
        pcm, log = decode(opts.ffmpeg, name, ["-map", "0:a:0", "-af", "ashowinfo",
                          "-c:a", "pcm_s16le", "-threads:a", "1", "-f", "s16le", "pipe:1"])
        require("Audio: aac (LC)" in log, f"{name}: pinned AAC profile")
        frames = re.findall(r"\bn:(\d+)\s+pts:(\d+)\s+pts_time:\S+.*?"
                            r"\bchannels:(\d+).*?\brate:(\d+)\s+nb_samples:(\d+)", log)
        require(len(frames) == 29 and len(pcm) == 29 * 1024 * 4,
                f"{name}: edit-list priming, 29 stereo blocks")
        for i, frame in enumerate(frames):
            require(tuple(map(int, frame)) == (i, i * 1024, 2, 48000, 1024),
                    f"{name}: actual AAC source PTS/rate/layout")
        if audio_reference is None:
            audio_reference = pcm
        require(pcm == audio_reference, f"{name}: remux/filler leaves AAC unchanged")
        packets, unused = decode(opts.ffmpeg, name, ["-map", "0:v:0", "-c", "copy",
                                "-f", "framecrc", "pipe:1"])
        sizes = [int(line.split(",")[4]) for line in packets.decode("ascii").splitlines()
                 if line and not line.startswith("#")]
        require(len(sizes) == 3 and (max(sizes) > 262144) == ("limit" in name),
                f"{name}: packet guard negative/control")
        print(f"ORACLE {name}: {types}, 3 video + 29 AAC blocks, max packet {max(sizes)} bytes")
    print("Mixed oracle PASS; not a WM6 PCM byte reference or scheduling/performance proof.")


if __name__ == "__main__":
    main()
