"""Read-only AAC sample-description transition oracle; not an ARM PCM reference."""
import argparse
import hashlib
import json
from pathlib import Path

import media_fixtures
from test_media_av_clock_fixtures import boxes
from test_media_audio_change_fixtures import decoded_frames
from test_media_mpeg4_fixtures import decode, require


def inspect(data):
    def child(box, kind):
        return next(b for b in boxes(data, box[0] + 8, box[0] + box[1]) if b[2] == kind)
    top = list(boxes(data, 0, len(data)))
    moov = next(b for b in top if b[2] == b"moov")
    mdat = next(b for b in top if b[2] == b"mdat")
    tracks = {}
    for track in boxes(data, moov[0] + 8, moov[0] + moov[1]):
        if track[2] != b"trak":
            continue
        mdia = child(track, b"mdia")
        handler = child(mdia, b"hdlr")
        kind = data[handler[0] + 16:handler[0] + 20]
        stbl = child(child(mdia, b"minf"), b"stbl")
        tables = {box[2]: data[box[0] + 8:box[0] + box[1]]
                  for box in boxes(data, stbl[0] + 8, stbl[0] + stbl[1])}
        mdhd = child(mdia, b"mdhd")
        tables[b"_mdhd"] = data[mdhd[0] + 8:mdhd[0] + mdhd[1]]
        tracks[kind] = tables
    require(set(tracks) == {b"vide", b"soun"}, "Exactly two tracks")
    return mdat[0], data[mdat[0] + 8:mdat[0] + mdat[1]], tracks


def raw_units(name):
    data = (media_fixtures.DEST / name).read_bytes()
    result = []
    position = 0
    while position < len(data):
        header = data[position:position + 7]
        require(len(header) == 7 and header[:2] == b"\xff\xf1", "Pinned ADTS header")
        length = ((header[3] & 3) << 11) | (header[4] << 3) | (header[5] >> 5)
        require(length > 7 and position + length <= len(data), "Whole ADTS frame")
        result.append(data[position + 7:position + length])
        position += length
    require(len(result) in (27, 30), "Pinned source unit count")
    return result


def u32(data, start):
    return int.from_bytes(data[start:start + 4], "big")


def video(exe, name):
    return decode(exe, name, ["-copyts", "-map", "0:v:0", "-fps_mode", "passthrough", "-c:v", "rawvideo",
                             "-pix_fmt", "yuv420p", "-threads:v", "1", "-f", "rawvideo", "pipe:1"])[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"], "Pinned tool")
    source = "mixed-clock-video-late.mp4"
    base_offset, base_payload, base_tracks = inspect((media_fixtures.DEST / source).read_bytes())
    base_pixels = video(opts.ffmpeg, source)
    units = [raw_units(name) for name in ("aac-lc.aac", "aac-lc-stereo.aac", "aac-lc-44100.aac")]
    rates, channels, configs = (48000, 48000, 44100), (1, 2, 1), (0x1188, 0x1190, 0x1208)
    cases = {"repeat": [0, 0], "stereo": [0, 1], "mono": [1, 0], "return": [0, 1, 0], "rate-down": [0, 2]}
    for stem, segments in cases.items():
        name = "mixed-config-" + stem + ".mp4"
        data = (media_fixtures.DEST / name).read_bytes()
        offset, payload, tracks = inspect(data)
        expected = [unit for index in segments for unit in units[index]]
        require(payload == base_payload + b"".join(expected), f"{name}: all source payloads unchanged")
        growth = offset - base_offset
        video_tables = tracks[b"vide"].copy()
        new_offsets = video_tables.pop(b"stco")
        old_tables = base_tracks[b"vide"].copy()
        old_offsets = old_tables.pop(b"stco")
        require(video_tables == old_tables and new_offsets[:8] == old_offsets[:8] and
                [u32(new_offsets, i) for i in range(8, len(new_offsets), 4)] ==
                [u32(old_offsets, i) + growth for i in range(8, len(old_offsets), 4)], "Video tables/chunks")
        tables = tracks[b"soun"]
        require(u32(tables[b"_mdhd"], 12) == (7056000 if stem == "rate-down" else 48000), "Audio timescale")
        descriptions = list(boxes(tables[b"stsd"], 8, len(tables[b"stsd"])))
        require(u32(tables[b"stsd"], 4) == len(segments) == len(descriptions), "Description count")
        for index, box in zip(segments, descriptions):
            entry = tables[b"stsd"][box[0]:box[0] + box[1]]
            require(entry[4:8] == b"mp4a" and int.from_bytes(entry[24:26], "big") == channels[index] and
                    u32(entry, 32) == rates[index] << 16 and b"\x05\x02" + configs[index].to_bytes(2, "big") in entry,
                    "AAC-LC ASC and sample entry")
        stsc = tables[b"stsc"]
        require([u32(stsc, i) for i in range(0, len(stsc), 4)] ==
                [0, len(segments)] + [v for i in range(len(segments)) for v in (i * 30 + 1, 1, i + 1)], "Chunk transitions")
        timing = [0, 2, 30, 150528, 27, 163840] if stem == "rate-down" else [0, 1, len(expected), 1024]
        require([u32(tables[b"stts"], i) for i in range(0, len(tables[b"stts"]), 4)] == timing, "Exact audio timing")
        require([u32(tables[b"stsz"], i) for i in range(12, len(tables[b"stsz"]), 4)] == [len(s) for s in expected], "Sample sizes")
        chunks = [u32(tables[b"stco"], i) for i in range(8, len(tables[b"stco"]), 4)]
        require(len(chunks) == len(expected) and all(data[start:start + len(sample)] == sample
                for start, sample in zip(chunks, expected)), "Referenced AAC units")
        require(video(opts.ffmpeg, name) == base_pixels and len(base_pixels) == 345600, "Three unchanged I420 frames")
        require(decoded_frames(opts.ffmpeg, name) == [(rates[index], channels[index], 1024) for index in segments for i in range(len(units[index]))],
                "Independent decoder observes exact channel transitions, not damaged payload")
        print(f"ORACLE {name}: exact descriptions {segments}, {len(expected)} AAC frames; all compressed data/video unchanged")
    print("Mixed config oracle PASS; WM6 rejection/prefix and lifecycle require device gates.")


if __name__ == "__main__":
    main()
