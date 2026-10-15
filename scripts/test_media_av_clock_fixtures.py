"""Read-only MP4 edit-list/timeline oracle, not a WM6 scheduler reference."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


def boxes(data, start, end):
    while start < end:
        size = int.from_bytes(data[start:start + 4], "big")
        require(size >= 8 and start + size <= end, "Complete fixed 32-bit box")
        yield start, size, data[start + 4:start + 8]
        start += size
    require(start == end, "Whole box boundary")


def metadata(data):
    def child(box, kind):
        start, size, unused = box
        return next(b for b in boxes(data, start + 8, start + size) if b[2] == kind)

    top = list(boxes(data, 0, len(data)))
    moov = next(b for b in top if b[2] == b"moov")
    mdat = next(b for b in top if b[2] == b"mdat")
    tracks = {}
    for track in boxes(data, moov[0] + 8, moov[0] + moov[1]):
        if track[2] != b"trak":
            continue
        mdia = child(track, b"mdia")
        hdlr = child(mdia, b"hdlr")
        kind = data[hdlr[0] + 16:hdlr[0] + 20]
        elst = child(child(track, b"edts"), b"elst")
        require(data[elst[0] + 8:elst[0] + 12] == bytes(4), "v0 edit list")
        count = int.from_bytes(data[elst[0] + 12:elst[0] + 16], "big")
        edits = []
        for index in range(count):
            position = elst[0] + 16 + index * 12
            edits.append((int.from_bytes(data[position:position + 4], "big"),
                          int.from_bytes(data[position + 4:position + 8], "big", signed=True),
                          int.from_bytes(data[position + 8:position + 12], "big")))
        stbl = child(child(mdia, b"minf"), b"stbl")
        tables = {box[2]: data[box[0]:box[0] + box[1]]
                  for box in boxes(data, stbl[0] + 8, stbl[0] + stbl[1]) if box[2] != b"stco"}
        tracks[kind] = edits, tables
    require(set(tracks) == {b"vide", b"soun"}, "Exactly video/audio tracks")
    return data[mdat[0] + 8:mdat[0] + mdat[1]], tracks


def actual_output(exe, name):
    pixels, video_log = decode(exe, name, ["-copyts", "-map", "0:v:0", "-vf", "showinfo",
                              "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt", "yuv420p",
                              "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
    require("Video: h264 (Constrained Baseline)" in video_log, "Pinned AVC profile")
    video = re.findall(r"\bn:\s*(\d+)\s+pts:\s*\S+\s+pts_time:(\S+).*?"
                       r"\bfmt:yuv420p.*?\bs:320x240\s+i:P\s+iskey:\d\s+type:(\w)", video_log)
    pcm, audio_log = decode(exe, name, ["-copyts", "-map", "0:a:0", "-af", "ashowinfo",
                           "-c:a", "pcm_s16le", "-threads:a", "1", "-f", "s16le", "pipe:1"])
    require("Audio: aac (LC)" in audio_log, "Pinned AAC-LC profile")
    audio = re.findall(r"\bn:(\d+)\s+pts:(-?\d+)\s+pts_time:\S+.*?"
                       r"\bchannels:(\d+).*?\brate:(\d+)\s+nb_samples:(\d+)", audio_log)
    return pixels, pcm, video, audio


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the existing pinned desktop tool")
    source = "baseline-aac.mp4"
    base_mdat, base_tracks = metadata((media_fixtures.DEST / source).read_bytes())
    base_pixels, base_pcm, unused_video, unused_audio = actual_output(opts.ffmpeg, source)
    cases = {"video-late": (400, 0), "audio-late": (0, 400), "shifted": (600, 600)}
    for stem, (video_ms, audio_ms) in cases.items():
        name = "mixed-clock-" + stem + ".mp4"
        mdat, tracks = metadata((media_fixtures.DEST / name).read_bytes())
        require(mdat == base_mdat, f"{name}: every encoded media byte unchanged")
        for kind, delay in ((b"vide", video_ms), (b"soun", audio_ms)):
            edits, tables = tracks[kind]
            expected = ([(delay, -1, 65536)] if delay else []) + base_tracks[kind][0]
            require(edits == expected and tables == base_tracks[kind][1],
                    f"{name}: empty edit only, preserve priming/sample tables/codec data")
        pixels, pcm, video, audio = actual_output(opts.ffmpeg, name)
        require(pixels == base_pixels and len(pixels) == 320 * 240 * 3 // 2 * 3,
                f"{name}: all visible I420 bytes unchanged")
        require(pcm == base_pcm and len(pcm) == 29 * 1024 * 4,
                f"{name}: 29 actual stereo blocks, priming and every PCM byte unchanged")
        require(len(video) == 3 and len(audio) == 29, f"{name}: decoded output counts")
        for index, (number, pts, kind) in enumerate(video):
            require(int(number) == index and kind == "IPP"[index] and
                    Decimal(pts) * 1000000 == video_ms * 1000 + index * 200000,
                    f"{name}: source video timeline")
        for index, frame in enumerate(audio):
            require(tuple(map(int, frame)) == (index, audio_ms * 48 + index * 1024, 2, 48000, 1024),
                    f"{name}: actual source audio timestamps/layout")
        print(f"ORACLE {name}: unchanged 3 video/29 audio blocks, source starts {video_ms}/{audio_ms} ms")
    print("Mixed timeline oracle PASS; WM6 source-PTS delivery and lifecycle require separate gates.")


if __name__ == "__main__":
    main()
