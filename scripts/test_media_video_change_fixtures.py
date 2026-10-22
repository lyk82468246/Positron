"""Read-only fixed AVC parameter/framing oracle; not a WM6 lifecycle reference."""
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


def nal_units(sample, length_bytes):
    position = 0
    result = []
    while position < len(sample):
        require(position + length_bytes <= len(sample), "Complete NAL length")
        size = int.from_bytes(sample[position:position + length_bytes], "big")
        position += length_bytes
        require(size > 0 and position + size <= len(sample), "Complete nonempty NAL payload")
        result.append(sample[position:position + size])
        position += size
    require(position == len(sample), "No trailing framing bytes")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--external", action="store_true", help="Check AVC1 stsd transitions instead of in-band SPS")
    mode.add_argument("--nal-switch", action="store_true", help="Check four-to-two byte lengths and in-band parameters")
    parser.add_argument("--returning", action="store_true", help="Check three segments returning to original parameters")
    args = parser.parse_args()
    if args.returning and args.nal_switch:
        parser.error("Return controls retain four-byte NAL lengths")
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
        if args.returning and stem not in ("repeat", "vga"):
            continue
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
        if not args.external:
            second[0] = parameter_sets(source_tables) + second[0]
        expected_samples = first + second
        if args.returning:
            third = list(first)
            if not args.external:
                third[0] = parameter_sets(base_tracks[b"vide"]) + third[0]
            expected_samples += third
        count = len(expected_samples)
        name = (("mixed-return-avcc-" if args.external else "mixed-return-inband-") if args.returning else
                "mixed-nal-" if args.nal_switch else "mixed-avcc-" if args.external else "mixed-video-") + stem + ".mp4"
        data = (media_fixtures.DEST / name).read_bytes()
        unused, payload, tracks = inspect(data)
        actual = samples(data, tracks[b"vide"])
        if args.nal_switch:
            descriptions = tracks[b"vide"][b"stsd"]
            entries = list(boxes(descriptions, 8, len(descriptions)))
            require(len(entries) == 2, "Two AVC3 sample descriptions")
            for box, length in zip(entries, (4, 2)):
                entry = descriptions[box[0]:box[0] + box[1]]
                avcc = next(b for b in boxes(entry, 86, len(entry)) if b[2] == b"avcC")
                config = entry[avcc[0] + 8:avcc[0] + avcc[1]]
                require(entry[4:8] == b"avc3" and config[0] == 1 and (config[4] & 3) + 1 == length,
                        "Explicit AVC3 lengthSizeMinusOne selects 4 then 2 bytes")
            require(actual[:3] == first and [nal_units(s, 2) for s in actual[3:]] ==
                    [nal_units(s, 4) for s in second], "Only length prefixes change; all SPS/PPS/slice NAL bytes identical")
            require(payload == prefix + b"".join(actual), "Exact original mdat prefix and all referenced video samples")
        else:
            require(payload == prefix + b"".join(expected_samples), "Original mdat and all AVC slices unchanged")
            require(actual == expected_samples, "Every AVC chunk points to exact source bytes")
        expected_stsd = bytearray(base_tracks[b"vide"][b"stsd"])
        if args.nal_switch:
            expected_stsd[12:16] = b"avc3"
            second_entry = bytearray(expected_stsd[8:])
            config_box = next(b for b in boxes(second_entry, 86, len(second_entry)) if b[2] == b"avcC")
            second_entry[config_box[0] + 12] = 0xfd
            expected_stsd[4:8] = (2).to_bytes(4, "big")
            expected_stsd += second_entry
        elif args.external:
            expected_stsd[4:8] = (2).to_bytes(4, "big")
            expected_stsd += source_tables[b"stsd"][8:]
        else:
            expected_stsd[12:16] = b"avc3"
        if args.external or args.nal_switch:
            require(tracks[b"vide"][b"stsc"] == bytes.fromhex(
                "0000000000000003000000010000000100000001000000040000000100000002000000070000000100000001" if args.returning else
                "0000000000000002000000010000000100000001000000040000000100000002"),
                "Exact sample-description transitions, including original entry reuse on return")
        else:
            require(tracks[b"vide"][b"stsc"] == bytes.fromhex("0000000000000001000000010000000100000001"),
                    "One in-band sample description")
        require(tracks[b"vide"][b"stsd"] == bytes(expected_stsd), "Exact original source descriptions/avcC")
        require(tracks[b"vide"][b"stts"] == bytes.fromhex("0000000000000001") + count.to_bytes(4, "big") + bytes.fromhex("00000800") and
                tracks[b"vide"][b"stss"] == bytes.fromhex("0000000000000003000000010000000400000007" if args.returning else
                                                          "00000000000000020000000100000004"),
                "Exact 2048-tick sample count with every segment IDR marked")
        old_audio = {k: v for k, v in base_tracks[b"soun"].items() if k != b"stco"}
        new_audio = {k: v for k, v in tracks[b"soun"].items() if k != b"stco"}
        require(old_audio == new_audio and samples(data, tracks[b"soun"]) == samples(base, base_tracks[b"soun"]), "AAC tables/payload unchanged")
        frames = output(args.ffmpeg, name)
        expected = first_frames + output(args.ffmpeg, source)
        if args.returning:
            expected += first_frames
        require(len(frames) == count and [f[1:] for f in frames] == [f[1:] for f in expected],
                "Every decoded frame: exact layout, field, type and plane checksums")
        require([Decimal(f[0]) * 1000 for f in frames] == [400 + i * 200 for i in range(count)], "Exact source video timeline")
        pcm = decode(args.ffmpeg, name, ["-map", "0:a:0", "-c:a", "pcm_s16le", "-f", "s16le", "pipe:1"])[0]
        require(pcm == base_pcm and len(pcm) == 29 * 1024 * 4, "Unchanged 29 AAC blocks including priming trim")
        print(f"ORACLE {name}: {count} source frames/plane checksums, unchanged AAC and exact PTS")
    print("Video change oracle PASS; device output guards/scheduling require WM6 gates.")


if __name__ == "__main__":
    main()
