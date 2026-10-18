"""Read-only fixed AVC3 transition oracle; not a WM6 lifecycle reference."""
import argparse
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

import media_fixtures
from test_media_av_clock_fixtures import boxes
from test_media_mixed_config_fixtures import inspect, u32
from test_media_mpeg4_fixtures import decode, require


def samples(data, tables):
    sizes = [u32(tables[b"stsz"], i) for i in range(12, len(tables[b"stsz"]), 4)]
    runs = [tuple(u32(tables[b"stsc"], i + j) for j in (0, 4, 8))
            for i in range(8, len(tables[b"stsc"]), 12)]
    result = []
    for number, i in enumerate(range(8, len(tables[b"stco"]), 4), 1):
        start = u32(tables[b"stco"], i)
        run = next(r for r in reversed(runs) if r[0] <= number)
        for unused in range(run[1]):
            size = sizes[len(result)]
            result.append(data[start:start + size])
            start += size
    require(len(result) == len(sizes), "Every fixed sample referenced")
    return result


def parameter_sets(tables):
    entry = tables[b"stsd"][8:]
    avcc = next(b for b in boxes(entry, 86, len(entry)) if b[2] == b"avcC")
    config = entry[avcc[0] + 8:avcc[0] + avcc[1]]
    require(config[0] == 1 and config[4] & 3 == 3, "Four-byte AVC lengths")
    position = 6
    result = []
    for count in (config[5] & 31, None):
        if count is None:
            count = config[position]
            position += 1
        for unused in range(count):
            size = int.from_bytes(config[position:position + 2], "big")
            position += 2
            result.append(size.to_bytes(4, "big") + config[position:position + size])
            position += size
    return b"".join(result)


def output(exe, name):
    unused, log = decode(exe, name, ["-copyts", "-map", "0:v:0", "-vf", "showinfo",
                                   "-fps_mode", "passthrough", "-f", "null", "-"])
    frames = re.findall(r"\bn:\s*\d+\s+pts:\s*\S+\s+pts_time:(\S+).*?"
                        r"\bfmt:(\S+).*?\bs:(\d+x\d+)\s+i:(\w)\s+iskey:(\d)\s+type:(\w).*?"
                        r"\bchecksum:(\w+)\s+plane_checksum:\[([^]]+)\]", log)
    return frames


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    args = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(args.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"], "Pinned tool")
    base = (media_fixtures.DEST / "baseline-aac.mp4").read_bytes()
    unused, prefix, base_tracks = inspect(base)
    first = samples(base, base_tracks[b"vide"])
    first_frames = output(args.ffmpeg, "baseline-aac.mp4")
    base_pcm = decode(args.ffmpeg, "baseline-aac.mp4", ["-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"])[0]
    cases = {"repeat": "baseline-aac.mp4", "vga": "main-vga.mp4", "oversize": "oversize.mp4",
             "high": "high.mp4", "high422": "high422.mp4", "interlaced": "interlaced.mp4"}
    for stem, source in cases.items():
        source_data = (media_fixtures.DEST / source).read_bytes()
        # The source-only inputs have no audio; inspect that one track directly.
        source_tables = None
        moov = next(b for b in boxes(source_data, 0, len(source_data)) if b[2] == b"moov")
        def visit(start, end):
            nonlocal source_tables
            for b in boxes(source_data, start, end):
                if b[2] == b"stbl" and source_tables is None:
                    source_tables = {t[2]: source_data[t[0] + 8:t[0] + t[1]] for t in boxes(source_data, b[0] + 8, b[0] + b[1])}
                elif b[2] in (b"moov", b"trak", b"mdia", b"minf"):
                    visit(b[0] + 8, b[0] + b[1])
        visit(moov[0], moov[0] + moov[1])
        second = samples(source_data, source_tables)
        second[0] = parameter_sets(source_tables) + second[0]
        name = "mixed-video-" + stem + ".mp4"
        data = (media_fixtures.DEST / name).read_bytes()
        unused, payload, tracks = inspect(data)
        require(payload == prefix + b"".join(first + second), "Original mdat and all AVC slices unchanged")
        require(samples(data, tracks[b"vide"]) == first + second, "Every AVC chunk points to exact source bytes")
        expected_stsd = bytearray(base_tracks[b"vide"][b"stsd"])
        expected_stsd[12:16] = b"avc3"
        require(tracks[b"vide"][b"stsd"] == bytes(expected_stsd), "Only AVC3 tag changes; initial avcC intact")
        require(tracks[b"vide"][b"stts"] == bytes.fromhex("00000000000000010000000600000800") and
                tracks[b"vide"][b"stss"] == bytes.fromhex("00000000000000020000000100000004"),
                "Six 2048-tick samples with both source IDRs marked")
        old_audio = {k: v for k, v in base_tracks[b"soun"].items() if k != b"stco"}
        new_audio = {k: v for k, v in tracks[b"soun"].items() if k != b"stco"}
        require(old_audio == new_audio and samples(data, tracks[b"soun"]) == samples(base, base_tracks[b"soun"]), "AAC tables/payload unchanged")
        frames = output(args.ffmpeg, name)
        expected = first_frames + output(args.ffmpeg, source)
        require(len(frames) == 6 and [f[1:] for f in frames] == [f[1:] for f in expected],
                "Six valid decoded frames: exact layout, field, type and plane checksums")
        require([Decimal(f[0]) * 1000 for f in frames] == [400 + i * 200 for i in range(6)], "Exact source video timeline")
        pcm = decode(args.ffmpeg, name, ["-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"])[0]
        require(pcm == base_pcm and len(pcm) == 29 * 1024 * 4, "Unchanged 29 AAC blocks including priming trim")
        print(f"ORACLE {name}: six source frames/plane checksums, unchanged AAC and exact PTS")
    print("Video change oracle PASS; device output guards/scheduling require WM6 gates.")


if __name__ == "__main__":
    main()
