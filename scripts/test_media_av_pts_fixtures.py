"""Read-only fixed MP4 CTTS/payload oracle; desktop PTS is not WM6 best-effort PTS."""
import argparse
import hashlib
import json
from pathlib import Path

import media_fixtures
from test_media_av_clock_fixtures import actual_output, boxes
from test_media_mpeg4_fixtures import require


def inspect(data, changed):
    def child(box, kind):
        return next(b for b in boxes(data, box[0] + 8, box[0] + box[1]) if b[2] == kind)

    top = list(boxes(data, 0, len(data)))
    moov = next(b for b in top if b[2] == b"moov")
    mdat = next(b for b in top if b[2] == b"mdat")
    tracks = {}
    offsets = []
    ctts = None
    for track in boxes(data, moov[0] + 8, moov[0] + moov[1]):
        if track[2] != b"trak":
            continue
        mdia = child(track, b"mdia")
        hdlr = child(mdia, b"hdlr")
        kind = data[hdlr[0] + 16:hdlr[0] + 20]
        mdhd = child(mdia, b"mdhd")
        require(data[mdhd[0] + 8] == 0, "Fixed v0 media header")
        require(int.from_bytes(data[mdhd[0] + 20:mdhd[0] + 24], "big") ==
                (10240 if kind == b"vide" else 48000), "Unchanged media timescale")
        edit = child(track, b"free" if changed and kind == b"vide" else b"edts")
        stbl = child(child(mdia, b"minf"), b"stbl")
        tables = {}
        for box in boxes(data, stbl[0] + 8, stbl[0] + stbl[1]):
            start, size, name = box
            if name == b"stco":
                count = int.from_bytes(data[start + 12:start + 16], "big")
                offsets.extend(int.from_bytes(data[start + 16 + i * 4:start + 20 + i * 4], "big")
                               for i in range(count))
            elif name == b"ctts":
                require(changed and kind == b"vide" and ctts is None and size == 40,
                        "Only one inserted video CTTS")
                require(data[start + 8:start + 16] == b"\x01\x00\x00\x00\x00\x00\x00\x03",
                        "Signed v1 CTTS, exactly three entries")
                ctts = []
                for i in range(3):
                    position = start + 16 + i * 8
                    require(data[position:position + 4] == b"\x00\x00\x00\x01", "One sample per entry")
                    ctts.append(int.from_bytes(data[position + 4:position + 8], "big", signed=True))
            else:
                tables[name] = data[start:start + size]
        tracks[kind] = data[edit[0] + 8:edit[0] + edit[1]], tables
    require(set(tracks) == {b"vide", b"soun"}, "Exactly two tracks")
    return data[mdat[0] + 8:mdat[0] + mdat[1]], tracks, offsets, ctts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", type=Path, required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(opts.ffmpeg.read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the existing pinned desktop tool")
    source = "baseline-aac.mp4"
    base = (media_fixtures.DEST / source).read_bytes()
    base_mdat, base_tracks, base_offsets, unused = inspect(base, False)
    base_pixels, base_pcm, unused_video, base_audio = actual_output(opts.ffmpeg, source)
    # Deliberately separate from the generator: assert each intended mutation.
    cases = {"negative": [-1024, -1024, -1024], "backward": [4096, 0, 0],
             "gap": [0, 4096, 4096], "duplicate": [2048, 0, 0], "fallback": [0, -3072, 0]}
    for stem, expected in cases.items():
        name = "mixed-pts-" + stem + ".mp4"
        data = (media_fixtures.DEST / name).read_bytes()
        mdat, tracks, offsets, ctts = inspect(data, True)
        require(len(data) == len(base) + 40 and mdat == base_mdat and tracks == base_tracks,
                f"{name}: unchanged payload, codec/sample tables and original edit contents")
        require(ctts == expected and offsets == [value + 40 for value in base_offsets],
                f"{name}: exact signed composition offsets and relocated chunks")
        pixels, pcm, video, audio = actual_output(opts.ffmpeg, name)
        require(pixels == base_pixels and len(pixels) == 345600 and pcm == base_pcm and len(pcm) == 118784,
                f"{name}: every visible I420/PCM byte and AAC priming unchanged")
        require(len(video) == 3 and [frame[2] for frame in video] == list("IPP") and audio == base_audio,
                f"{name}: complete decoded frames and 29 audio blocks/timestamps")
        print(f"ORACLE {name}: CTTS={ctts}; unchanged 3/29 full output; desktop video PTS={[v[1] for v in video]}")
    print("Mixed PTS oracle PASS; CLI can normalize timestamps. WM6 guard/prefix/fallback need separate gates.")


if __name__ == "__main__":
    main()
