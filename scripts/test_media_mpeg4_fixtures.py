"""Check the pinned MPEG-4 Part 2 corpus, not WM6 product support.

Without --ffmpeg, verify hashes only. With the manifest's explicit generator,
independently decode pixels, PTS/durations, frame types and AVI MP3 samples.
Never regenerate a fixture, download a tool, or launch a device/build here.
"""
import argparse
from decimal import Decimal
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import media_fixtures


CASES = {
    "mpeg4-simple.mp4": (320, 240, "Simple Profile", "PPP", "IPP", (0, 200000, 400000)),
    "mpeg4-asp-vga.mp4": (640, 480, "Advanced Simple Profile", "PPP", "IBP", (0, 200000, 400000)),
    "mpeg4-mp3.avi": (320, 240, "Simple Profile", "PPP", "IPP", (0, 400000, 600000)),
    "mpeg4-interlaced.mp4": (320, 240, "Advanced Simple Profile", "TTT", "IBP", (0, 200000, 400000)),
    "mpeg4-oversize.mp4": (656, 480, "Simple Profile", "PPP", "IPP", (0, 200000, 400000)),
}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def decode(exe, name, args):
    flags = (subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW) if os.name == "nt" else 0
    result = subprocess.run(
        [str(exe), "-hide_banner", "-nostdin", "-threads", "1", "-i",
         str(media_fixtures.DEST / name)] + args,
        check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        creationflags=flags, timeout=30)
    return result.stdout, result.stderr.decode("utf-8", errors="replace")


def microseconds(value):
    scaled = Decimal(value) * 1000000
    require(scaled == scaled.to_integral_value(), "Nonintegral fixture timestamp")
    return int(scaled)


def video_oracle(exe, name, case):
    width, height, profile, interlace, frame_types, pts = case
    raw, log = decode(exe, name, ["-map", "0:v:0", "-vf", "showinfo", "-fps_mode", "passthrough",
                                "-c:v", "rawvideo", "-pix_fmt", "yuv420p", "-threads:v", "1",
                                "-f", "rawvideo", "pipe:1"])
    require(f"Video: mpeg4 ({profile})" in log, f"{name}: unexpected codec/profile")
    pattern = (r"n:\s*(\d+)\s+pts:\s*\S+\s+pts_time:(\S+)\s+duration:\s*\S+"
               r"\s+duration_time:(\S+)\s+fmt:(\S+).*?\bs:(\d+)x(\d+)"
               r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)")
    frames = re.findall(pattern, log)
    require(len(frames) == 3, f"{name}: expected exactly three decoded frames")
    plane_sizes = (width * height, width * height // 4, width * height // 4)
    frame_size = sum(plane_sizes)
    require(len(raw) == frame_size * 3, f"{name}: unexpected raw pixel count")
    for index, frame in enumerate(frames):
        number, start, duration, fmt, w, h, field, key, kind = frame
        require(int(number) == index and (int(w), int(h)) == (width, height), f"{name}: dimensions/order")
        require(fmt == "yuv420p" and field == interlace[index], f"{name}: pixel/field format")
        require(kind == frame_types[index] and int(key) == (index == 0), f"{name}: frame type/key flag")
        require(microseconds(start) == pts[index] and microseconds(duration) == 200000,
                f"{name}: PTS/duration")
        offset = index * frame_size
        for count, expected in zip(plane_sizes, (81, 90, 240)):
            plane = raw[offset:offset + count]
            require(min(plane) >= expected - 1 and max(plane) <= expected + 1,
                    f"{name}: unexpected red I420 pixels")
            offset += count
    print(f"ORACLE {name}: {width}x{height}, {profile}, {frame_types}, PTS={pts}, I420=81/90/240")


def audio_oracle(exe):
    name = "mpeg4-mp3.avi"
    pcm, log = decode(exe, name, ["-map", "0:a:0", "-af", "ashowinfo", "-c:a", "pcm_s16le",
                                "-threads:a", "1", "-f", "s16le", "pipe:1"])
    blocks = re.findall(r"\bn:(\d+)\s+pts:\S+\s+pts_time:(\S+).*?\bchannels:(\d+)"
                        r".*?\brate:(\d+)\s+nb_samples:(\d+)", log)
    require(len(blocks) == 26 and len(pcm) == 29952 * 2 * 2, "AVI MP3 sample/block count")
    for index, block in enumerate(blocks):
        number, pts, channels, rate, samples = block
        # ashowinfo prints rounded seconds; these exact 24 ms steps need no tolerance.
        require((int(number), int(channels), int(rate), int(samples)) == (index, 2, 48000, 1152),
                "AVI MP3 layout")
        require(microseconds(pts) == index * 24000, "AVI MP3 timestamp")
    samples = [int.from_bytes(pcm[i:i + 2], "little", signed=True) for i in range(0, len(pcm), 2)]
    require(sum(abs(value) for value in samples) > 1000000, "AVI MP3 unexpectedly silent")
    print("ORACLE AVI MP3: stereo 48000 Hz, 26x1152 samples/channel, PTS=0..600000 step 24000")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    names = [record["file"] for record in pin["files"]]
    require(len(names) == len(set(names)), "Duplicate manifest entry")
    require(set(CASES) <= set(names), "Missing MPEG-4 Part 2 fixture")
    if opts.ffmpeg is None:
        print("Hashes OK; desktop decode not run (supply --ffmpeg). WM6 device gate remains separate.")
        return
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Desktop oracle must use the existing pinned generator")
    for name, case in CASES.items():
        video_oracle(opts.ffmpeg, name, case)
    audio_oracle(opts.ffmpeg)
    print("Desktop oracle PASS; does not establish WM6 decoding, seek or performance support.")


if __name__ == "__main__":
    main()
