"""Read-only AAC priming evidence using the pinned desktop fixture generator.

Compare fresh opens with CLI input seek-to-zero. This is NOT an in-session
WM6 decoder-flush test and cannot validate the product DLL or a proposed fix.
No generation, download, device access, or build is performed.
"""
import argparse
from array import array
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

import media_fixtures


CASES = {"baseline-aac.mp4": (2, 29696), "aac-lc.aac": (1, 30720)}


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def decode(exe, name, seek):
    flags = 0
    if os.name == "nt":
        flags = subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW
    command = [str(exe), "-hide_banner", "-nostdin", "-threads", "1", "-debug_ts"]
    if seek:
        command += ["-ss", "0"]
    command += ["-i", str(media_fixtures.DEST / name), "-map", "0:a:0",
                "-af", "ashowinfo", "-c:a", "pcm_s16le", "-threads:a", "1",
                "-f", "s16le", "pipe:1"]
    result = subprocess.run(command, check=True, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, creationflags=flags, timeout=30)
    log = result.stderr.decode("utf-8", errors="replace")
    packets = re.findall(r"demuxer -> .*?type:audio pkt_pts:(-?\d+)", log)
    frames = re.findall(r"\bn:(\d+)\s+pts:(-?\d+).*?\bchannels:(\d+)"
                        r".*?\brate:(\d+)\s+nb_samples:(\d+)", log)
    channels, count = CASES[name]
    require(packets and len(frames) == count // 1024, f"{name}: missing packet/frame trace")
    require(len(result.stdout) == count * channels * 2, f"{name}: PCM byte count")
    for index, frame in enumerate(frames):
        require(tuple(map(int, frame)) == (index, index * 1024, channels, 48000, 1024),
                f"{name}: unexpected PCM timeline/layout at block {index}")
    return result.stdout, tuple(map(int, packets)), tuple(frames)


def samples(pcm):
    values = array("h")
    values.frombytes(pcm)
    if sys.byteorder != "little":
        values.byteswap()
    return values


def summarize(name, mode, pcm, packets):
    print(f"{name} {mode}: first_packet_pts={packets[0]} "
          f"samples/channel={CASES[name][1]} "
          f"energy={sum(abs(value) for value in samples(pcm))} "
          f"sha256={hashlib.sha256(pcm).hexdigest()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the existing pinned desktop generator")
    for name, (channels, _) in CASES.items():
        record = next(entry for entry in pin["files"] if entry["file"] == name)
        data = (media_fixtures.DEST / name).read_bytes()
        require(len(data) == record["bytes"] and
                hashlib.sha256(data).hexdigest() == record["sha256"], f"{name}: input pin mismatch")
        fresh, packets, frames = decode(opts.ffmpeg, name, False)
        repeated, repeated_packets, repeated_frames = decode(opts.ffmpeg, name, False)
        require((fresh, packets, frames) == (repeated, repeated_packets, repeated_frames),
                f"{name}: independent fresh opens are not deterministic")
        sought, seek_packets, seek_frames = decode(opts.ffmpeg, name, True)
        require(frames == seek_frames, f"{name}: fresh/seek output metadata differs")
        summarize(name, "fresh", fresh, packets)
        summarize(name, "CLI seek0 (fresh decoder)", sought, seek_packets)
        before, after = samples(fresh), samples(sought)
        differences = [index for index, pair in enumerate(zip(before, after)) if pair[0] != pair[1]]
        first = differences[0] // channels if differences else None
        print(f"  differing interleaved samples={len(differences)} first sample/channel={first}")
        stride = 1024 * channels * 2
        changed = [index for index in range(len(frames))
                   if fresh[index * stride:(index + 1) * stride] !=
                   sought[index * stride:(index + 1) * stride]]
        print(f"  differing 1024-sample blocks={changed}")
    print("Desktop evidence checks PASS; this script does not validate WM6 in-session seek fidelity.")


if __name__ == "__main__":
    main()
