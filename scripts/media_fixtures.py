"""Offline procedural media fixtures; never run by the product build.

Use --generate --ffmpeg PATH only to intentionally replace the corpus/pin.
The default mode verifies committed byte counts and SHA-256 without FFmpeg.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import media_mixed_config_fixtures

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "test_host" / "fixtures" / "media"
PIN = DEST / "manifest.json"


def run(exe, args):
    return subprocess.run([exe, "-hide_banner", "-loglevel", "error", "-nostdin"] + args,
                          check=True, stdout=subprocess.PIPE).stdout


def extra_records(exe):
    records = []
    for name, pixels, audio in [("mjpeg-mp3.avi", "yuvj420p", True),
                                ("mjpeg422.avi", "yuvj422p", False)]:
        args = ["-f", "lavfi", "-i", "color=c=red:s=320x240:r=5:d=0.6"]
        if audio:
            args += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6"]
        args += ["-map", "0:v", "-c:v", "mjpeg", "-pix_fmt", pixels,
                 "-q:v", "2", "-threads:v", "1"]
        if audio:
            args += ["-map", "1:a", "-c:a", "libmp3lame", "-ac", "2",
                     "-b:a", "64k", "-threads:a", "1"]
        else:
            args += ["-an"]
        args += ["-t", "0.6", "-fflags", "+bitexact", "-flags:v", "+bitexact",
                 "-map_metadata", "-1", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    name = "mp3-mono.mp3"
    args = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=44100:duration=0.6",
            "-c:a", "libmp3lame", "-ac", "1", "-b:a", "64k", "-threads", "1",
            "-fflags", "+bitexact", "-map_metadata", "-1", "-id3v2_version", "0",
            "-y", str(DEST / name)]
    run(exe, args)
    records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def mpeg_records(exe):
    records = []
    cases = [("mpeg2-mp2.ts", "mpeg2video", "320x240", True, False, "mpegts"),
             ("mpeg1-mp2.mpg", "mpeg1video", "320x240", True, False, "mpeg"),
             ("mpeg2-interlaced.ts", "mpeg2video", "320x240", False, True, "mpegts"),
             ("mpeg2-oversize.ts", "mpeg2video", "656x480", False, False, "mpegts")]
    for name, codec, size, audio, interlaced, container in cases:
        args = ["-f", "lavfi", "-i", f"color=c=red:s={size}:r=25:d=0.12"]
        if audio:
            args += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.12"]
        args += ["-map", "0:v", "-c:v", codec, "-pix_fmt", "yuv420p",
                 "-q:v", "2", "-g", "12", "-bf", "2", "-threads:v", "1"]
        if interlaced:
            args += ["-top", "1"]
        if audio:
            args += ["-map", "1:a", "-c:a", "mp2", "-ac", "1", "-b:a", "64k", "-threads:a", "1"]
        else:
            args += ["-an"]
        args += ["-t", "0.12", "-fflags", "+bitexact", "-flags:v",
                 "+bitexact+ilme+ildct" if interlaced else "+bitexact",
                 "-map_metadata", "-1", "-output_ts_offset", "2",
                 "-muxdelay", "0", "-muxpreload", "0", "-f", container,
                 "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def amr_records(exe):
    records = []
    for name, codec, rate, bitrate in [
        ("amr-nb.amr", "libopencore_amrnb", 8000, 12200),
        ("amr-wb.amr", "libvo_amrwbenc", 16000, 23850),
    ]:
        args = ["-f", "lavfi", "-i", f"sine=frequency=440:sample_rate={rate}:duration=0.12",
                "-c:a", codec, "-ar", str(rate), "-ac", "1", "-b:a", str(bitrate),
                "-dtx", "0", "-threads:a", "1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                "-map_metadata", "-1", "-f", "amr", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def ima_records(exe):
    records = []
    for name, channels in [("ima-mono.wav", 1), ("ima-stereo.wav", 2)]:
        signal = "0.125*sin(2*PI*440*t)"
        if channels == 2:
            signal += "|0.0625*sin(2*PI*880*t)"
        args = ["-f", "lavfi", "-i", f"aevalsrc={signal}:s=8000:d=0.3",
                "-c:a", "adpcm_ima_wav", "-ar", "8000", "-ac", str(channels),
                "-block_size", "512", "-threads:a", "1", "-fflags", "+bitexact",
                "-flags:a", "+bitexact", "-map_metadata", "-1", "-f", "wav",
                "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    return records + ima_pcm_records(exe)


def ima_pcm_records(exe):
    records = []
    for name in ["ima-mono.wav", "ima-stereo.wav"]:
        pcm_name = name.replace(".wav", ".pcm")
        args = ["-i", str(DEST / name), "-c:a", "pcm_s16le", "-threads:a", "1",
                "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "s16le",
                "-y", str(DEST / pcm_name)]
        run(exe, args)
        records.append({"file": pcm_name, "arguments": [name if a == str(DEST / name) else
                        pcm_name if a == str(DEST / pcm_name) else a for a in args]})
    return records


def mpeg4_records(exe):
    records = []
    cases = [("mpeg4-simple.mp4", "320x240", 0, False, False, "mp4"),
             ("mpeg4-asp-vga.mp4", "640x480", 2, False, False, "mp4"),
             ("mpeg4-mp3.avi", "320x240", 0, True, False, "avi"),
             ("mpeg4-interlaced.mp4", "320x240", 2, False, True, "mp4"),
             ("mpeg4-oversize.mp4", "656x480", 0, False, False, "mp4")]
    for name, size, bframes, audio, interlaced, container in cases:
        args = ["-f", "lavfi", "-i", f"color=c=red:s={size}:r=5:d=0.6"]
        if audio:
            args += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6"]
        args += ["-map", "0:v", "-c:v", "mpeg4", "-pix_fmt", "yuv420p",
                 "-q:v", "2", "-g", "12", "-bf", str(bframes), "-threads:v", "1"]
        if interlaced:
            args += ["-top", "1"]
        if audio:
            args += ["-map", "1:a", "-c:a", "libmp3lame", "-ac", "2",
                     "-b:a", "64k", "-threads:a", "1"]
        else:
            args += ["-an"]
        args += ["-t", "0.6", "-fflags", "+bitexact", "-flags:v",
                 "+bitexact+ilme+ildct" if interlaced else "+bitexact",
                 "-map_metadata", "-1"]
        if container == "mp4":
            args += ["-movflags", "+faststart"]
        args += ["-f", container, "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def h263_records(exe):
    records = []
    for name, codec, size in [("h263-cif.avi", "h263", "352x288"),
                              ("h263p-vga.avi", "h263p", "640x480"),
                              ("h263-oversize.avi", "h263", "704x576")]:
        args = ["-f", "lavfi", "-i", f"color=c=red:s={size}:r=5:d=0.6",
                "-c:v", codec, "-pix_fmt", "yuv420p", "-q:v", "2", "-g", "12",
                "-bf", "0", "-threads:v", "1", "-an", "-t", "0.6",
                "-fflags", "+bitexact", "-flags:v", "+bitexact",
                "-map_metadata", "-1", "-f", "avi", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def flv_records(exe):
    records = []
    for name, source in [("flv-h264-aac.flv", "baseline-aac.mp4"),
                         ("flv-main-vga.flv", "main-vga.mp4"),
                         ("flv-oversize.flv", "oversize.mp4")]:
        args = ["-i", str(DEST / source), "-map", "0", "-c", "copy",
                "-fflags", "+bitexact", "-map_metadata", "-1", "-f", "flv",
                "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": [source if a == str(DEST / source)
                        else name if a == str(DEST / name) else a for a in args]})
    name = "flv1-unsupported.flv"
    args = ["-f", "lavfi", "-i", "color=c=red:s=320x240:r=5:d=0.6",
            "-c:v", "flv", "-pix_fmt", "yuv420p", "-q:v", "2", "-threads:v", "1",
            "-an", "-fflags", "+bitexact", "-flags:v", "+bitexact",
            "-map_metadata", "-1", "-f", "flv", "-y", str(DEST / name)]
    run(exe, args)
    records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def annexb_records(exe):
    records = []
    for stem in ["baseline-aac", "main-vga", "high", "high422", "interlaced", "oversize"]:
        source = stem + ".mp4"
        name = "annexb-" + stem + ".h264"
        args = ["-i", str(DEST / source), "-map", "0:v:0", "-c:v", "copy",
                "-bsf:v", "h264_mp4toannexb", "-an", "-fflags", "+bitexact",
                "-map_metadata", "-1", "-f", "h264", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": [source if a == str(DEST / source)
                        else name if a == str(DEST / name) else a for a in args]})
    return records


FLV_CLOCK = ["base", "unknown", "late-unknown", "backward", "negative", "gap", "duplicate"]

AUDIO_CHANGE = ["repeat", "rate-down", "rate-up", "stereo", "mono", "rate-return"]


def audio_change_records(exe):
    records = []
    for name, signal, rate, channels in [
        ("aac-lc-44100.aac", "sine=frequency=440:sample_rate=44100:duration=0.6", 44100, 1),
        ("aac-lc-stereo.aac", "aevalsrc=0.08*sin(2*PI*440*t)|0.06*sin(2*PI*660*t):s=48000:d=0.6", 48000, 2),
    ]:
        args = ["-f", "lavfi", "-i", signal, "-c:a", "aac", "-profile:a", "aac_low",
                "-ar", str(rate), "-ac", str(channels), "-b:a", "64k", "-threads", "1",
                "-fflags", "+bitexact", "-flags:a", "+bitexact", "-map_metadata", "-1",
                "-f", "adts", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    sources = [(DEST / name).read_bytes() for name in
               ["aac-lc.aac", "aac-lc-44100.aac", "aac-lc-stereo.aac"]]
    pieces = [(0, 0), (0, 1), (1, 0), (0, 2), (2, 0), (0, 1, 0)]
    for stem, segments in zip(AUDIO_CHANGE, pieces):
        name = "aac-clock-" + stem + ".aac"
        (DEST / name).write_bytes(b"".join(sources[i] for i in segments))
        records.append({"file": name, "fixture_transform": "whole pinned ADTS segment concatenation v1",
                        "segments": [["aac-lc.aac", "aac-lc-44100.aac", "aac-lc-stereo.aac"][i]
                                     for i in segments]})
    return records


def flv_clock_records(exe):
    # Fixture-only: preserve the pinned Baseline AVCC/video payloads, removing
    # audio/script tags and changing only FLV timestamp/composition fields.
    source = (DEST / "flv-h264-aac.flv").read_bytes()
    header = bytearray(source[:13])
    assert header[:5] == b"FLV\x01\x05" and header[5:9] == b"\x00\x00\x00\x09"
    header[4] = 1
    tags = []
    position = 13
    while position < len(source):
        size = int.from_bytes(source[position + 1:position + 4], "big")
        tag = source[position:position + size + 15]
        assert len(tag) == size + 15 and int.from_bytes(tag[-4:], "big") == size + 11
        if tag[0] == 9:
            assert size >= 5 and tag[11] & 15 == 7
            tags.append(tag)
        position += size + 15
    assert position == len(source)
    packets = [i for i, tag in enumerate(tags) if tag[12] == 1]
    assert len(packets) == 3
    assert [int.from_bytes(tags[i][4:7], "big") | (tags[i][7] << 24) for i in packets] == [21, 221, 421]
    assert all(tags[i][13:16] == b"\x00\x00\x00" for i in packets)
    records = []
    for stem in FLV_CLOCK:
        changed = [bytearray(tag) for tag in tags]
        for index, tag_index in enumerate(packets):
            tag = changed[tag_index]
            timestamp = [21, 221, 421][index]
            composition = 0
            if stem == "unknown" or (stem == "late-unknown" and index > 0):
                composition = 900001  # pinned demux invalidates PTS and DTS (>15 minutes)
            elif stem == "negative" and index == 0:
                composition = -22  # known -1 ms, not AV_NOPTS_VALUE
            elif stem == "backward" and index == 1:
                timestamp = 0
            elif stem == "gap":
                timestamp = [21, 621, 821][index]
            elif stem == "duplicate":
                timestamp = [21, 21, 421][index]
            tag[4:7] = (timestamp & 0xffffff).to_bytes(3, "big")
            tag[7] = timestamp >> 24
            tag[13:16] = (composition & 0xffffff).to_bytes(3, "big")
        name = "flv-clock-" + stem + ".flv"
        (DEST / name).write_bytes(bytes(header) + b"".join(changed))
        records.append({"file": name, "fixture_transform": "pinned Baseline FLV video-only timestamp controls v1"})
    return records


def annexb_sequence_records(exe):
    # Fixture-only mutation at pinned trace_headers bit offsets (NAL header included).
    source = (DEST / "annexb-baseline-aac.h264").read_bytes()
    units = re.split(b"(\x00\x00\x00?\x01)", source)
    changed = 0
    for index in range(2, len(units), 2):
        if not units[index] or units[index][0] & 31 != 7:
            continue
        rbsp = bytearray()
        zeros = 0
        for value in units[index]:
            if zeros == 2 and value == 3:
                zeros = 0
                continue
            rbsp.append(value)
            zeros = zeros + 1 if value == 0 else 0
        bits = "".join(format(value, "08b") for value in rbsp)
        if bits[75:141] != "1" + format(1, "032b") + format(10, "032b") + "0":
            raise SystemExit("Pinned baseline SPS timing offsets changed")
        bits = bits[:75] + "0" + bits[141:]
        bits = bits.rstrip("0")  # keep rbsp_stop_one_bit; replace byte alignment
        bits += "0" * (-len(bits) % 8)
        escaped = bytearray()
        zeros = 0
        for offset in range(0, len(bits), 8):
            value = int(bits[offset:offset + 8], 2)
            if zeros == 2 and value <= 3:
                escaped.append(3)
                zeros = 0
            escaped.append(value)
            zeros = zeros + 1 if value == 0 else 0
        units[index] = bytes(escaped)
        changed += 1
    if changed != 1:
        raise SystemExit("Expected one pinned baseline SPS")
    no_timing = b"".join(units)
    outputs = {"annexb-no-timing.h264": no_timing,
               "annexb-repeat-sps.h264": source + source,
               "annexb-change-no-timing.h264": source + no_timing}
    for stem in ["main-vga", "high", "high422", "interlaced", "oversize"]:
        outputs["annexb-change-" + stem + ".h264"] = source + (DEST / ("annexb-" + stem + ".h264")).read_bytes()
    records = []
    for name, data in outputs.items():
        (DEST / name).write_bytes(data)
        records.append({"file": name, "fixture_transform": "pinned Annex-B SPS timing removal/concatenation v1"})
    return records


def write_pin(exe, records):
    for record in records:
        data = (DEST / record["file"]).read_bytes()
        record.update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    version = subprocess.check_output([exe, "-version"], text=True).splitlines()[0]
    PIN.write_text(json.dumps({"origin": "procedural red/sine; no downloaded media",
                              "content_license": "CC0-1.0; no patent grant",
                              "generator_version": version,
                              "generator_sha256": hashlib.sha256(Path(exe).read_bytes()).hexdigest(),
                              "files": records}, indent=2) + "\n", encoding="utf-8")


ANNEXB_DAMAGE = ["bad-sps", "bad-pps", "missing-pps", "orphan-pps",
                 "late-bad-sps", "late-bad-pps", "cut-idr", "cut-p", "two-frames"]


def annexb_damage_records(exe):
    source = (DEST / "annexb-baseline-aac.h264").read_bytes()
    units = re.split(b"(\x00\x00\x00?\x01)", source)
    indices = {kind: [i for i in range(2, len(units), 2) if units[i][0] & 31 == kind]
               for kind in [7, 8, 5, 1]}
    assert [len(indices[k]) for k in [7, 8, 5, 1]] == [1, 1, 1, 2]

    def replace(kind, value):
        copy = units[:]
        for i in indices[kind]:
            copy[i - 1:i + 1] = [copy[i - 1], value]
        return b"".join(copy)

    def cut(index, payload_bytes):
        return b"".join(units[:index]) + units[index][:payload_bytes]

    outputs = [replace(7, units[indices[7][0]][:4]), replace(8, b"\x68\x80"),
               replace(8, b""), replace(7, b""),
               source + b"\x00\x00\x01\x67\x42\xc0\x0c",
               source + b"\x00\x00\x01\x68\x80",
               cut(indices[5][0], 12), cut(indices[1][-1], 4),
               b"".join(units[:indices[1][-1] - 1])]
    records = []
    for stem, data in zip(ANNEXB_DAMAGE, outputs):
        name = "annexb-" + stem + ".h264"
        (DEST / name).write_bytes(data)
        records.append({"file": name, "fixture_transform": "pinned Annex-B parameter/payload damage v1"})
    return records


ANNEXB_PARAMETERS = ["repeat-pps", "change-pps", "pps-id1", "dangling-pps",
                     "late-dangling-pps", "pps-missing-sps", "sps-4096", "sps-4097",
                     "pps-4096", "pps-4097", "late-pps-4097", "slice-before-pps"]


def annexb_parameter_records(exe):
    source = (DEST / "annexb-baseline-aac.h264").read_bytes()
    units = re.split(b"(\x00\x00\x00?\x01)", source)
    pps_index = next(i for i in range(2, len(units), 2) if units[i][0] & 31 == 8)

    def edit(unit, offset, value):
        # Pinned baseline has no emulation-prevention bytes in these NALs.
        assert b"\x00\x00\x03" not in unit
        bits = "".join(f"{b:08b}" for b in unit).rstrip("0")
        assert bits[offset] == "1"
        bits = bits[:offset] + value + bits[offset + 1:]
        bits += "0" * (-len(bits) % 8)
        raw = bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
        escaped = bytearray()
        zeros = 0
        for b in raw:
            if zeros == 2 and b <= 3:
                escaped.append(3)
                zeros = 0
            escaped.append(b)
            zeros = zeros + 1 if b == 0 else 0
        return bytes(escaped)

    def pps_replace(data):
        copy = units[:]
        copy[pps_index] = data
        return b"".join(copy)

    changed = bytearray(units[pps_index])
    assert changed[4] & 8 == 0  # trace_headers constrained_intra_pred_flag, bit 36
    changed[4] |= 8
    reference = units[:]
    for i in range(2, len(units), 2):
        kind = units[i][0] & 31
        if kind in (1, 5):
            reference[i] = edit(units[i], 16 if kind == 5 else 14, "010")
    dangling = b"".join(reference)
    reference[pps_index] = edit(units[pps_index], 8, "010")
    moved = units[:]
    moved[pps_index - 1:pps_index + 1] = [b"", b""]
    first_p = next(i for i in range(2, len(units), 2) if units[i][0] & 31 == 1)
    moved[first_p - 1:first_p - 1] = units[pps_index - 1:pps_index + 1]
    outputs = [pps_replace(units[pps_index] + b"\x00\x00\x01" + units[pps_index]),
               source + pps_replace(bytes(changed)), b"".join(reference), dangling,
               source + dangling, pps_replace(edit(units[pps_index], 9, "010"))]
    for size in (4096, 4097):
        copy = units[:]
        sps_index = next(i for i in range(2, len(units), 2) if units[i][0] & 31 == 7)
        # SPS id 32 passes the fixed raw probe's <=32 check but the SPS parser
        # rejects it. Keep the real profile/constraint/level prefix.
        copy[sps_index] = units[sps_index][:4] + b"\x04\x20" + b"\x55" * (size - 6)
        outputs.append(source + b"".join(copy))
    # PPS id 256 likewise crosses the parser's real 0..255 identity boundary.
    outputs += [source + pps_replace(b"\x68\x00\x80\xc0" + b"\x55" * (size - 4)) for size in (4096, 4097)]
    outputs += [source + b"\x00\x00\x01\x68\x00\x80\xc0" + b"\x55" * 4093, b"".join(moved)]
    assert len(outputs) == len(ANNEXB_PARAMETERS)
    records = []
    for stem, data in zip(ANNEXB_PARAMETERS, outputs):
        name = "annexb-" + stem + ".h264"
        (DEST / name).write_bytes(data)
        records.append({"file": name, "fixture_transform": "pinned Annex-B PPS/reference/budget v1"})
    return records


def mixed_records(exe):
    records = []
    # Copy existing elementary packets, adding AAC to the reordered VGA case.
    name = "mixed-main-aac.mp4"
    args = ["-i", str(DEST / "main-vga.mp4"), "-i", str(DEST / "baseline-aac.mp4"),
            "-map", "0:v:0", "-map", "1:a:0", "-c", "copy", "-fflags", "+bitexact",
            "-map_metadata", "-1", "-movflags", "+faststart", "-y", str(DEST / name)]
    run(exe, args)
    records.append({"file": name, "arguments": args[:-1] + [name]})
    return records


def mixed_limit_records(exe):
    # Fixed 32-bit ISO-BMFF fixture transform, not a product MP4 writer.
    data = bytearray((DEST / "baseline-aac.mp4").read_bytes())

    def boxes(start, end):
        while start < end:
            size = int.from_bytes(data[start:start + 4], "big")
            assert size >= 8 and start + size <= end
            yield start, size, bytes(data[start + 4:start + 8])
            start += size
        assert start == end

    def child(box, kind):
        start, size, unused = box
        return next(b for b in boxes(start + 8, start + size) if b[2] == kind)

    top = list(boxes(0, len(data)))
    moov = next(b for b in top if b[2] == b"moov")
    mdat = next(b for b in top if b[2] == b"mdat")
    tables = []
    for track in boxes(moov[0] + 8, moov[0] + moov[1]):
        if track[2] != b"trak":
            continue
        mdia = child(track, b"mdia")
        hdlr = child(mdia, b"hdlr")
        stbl = child(child(mdia, b"minf"), b"stbl")
        tables.append((bytes(data[hdlr[0] + 16:hdlr[0] + 20]),
                       child(stbl, b"stco"), child(stbl, b"stsz")))
    video = next(t for t in tables if t[0] == b"vide")
    offset = int.from_bytes(data[video[1][0] + 16:video[1][0] + 20], "big")
    assert moov[0] < mdat[0] < offset and video[2][0] < mdat[0]
    filler = b"\x0c" + b"\xff" * 262144 + b"\x80"
    payload = len(filler).to_bytes(4, "big") + filler

    def put(position, value):
        data[position:position + 4] = value.to_bytes(4, "big")

    size_pos = video[2][0] + 20
    assert int.from_bytes(data[video[2][0] + 12:video[2][0] + 16], "big") == 0
    put(size_pos, int.from_bytes(data[size_pos:size_pos + 4], "big") + len(payload))
    for unused, stco, unused_size in tables:
        count = int.from_bytes(data[stco[0] + 12:stco[0] + 16], "big")
        for index in range(count):
            position = stco[0] + 16 + index * 4
            value = int.from_bytes(data[position:position + 4], "big")
            if value > offset:
                put(position, value + len(payload))
    put(mdat[0], mdat[1] + len(payload))
    data[offset:offset] = payload
    name = "mixed-packet-limit.mp4"
    (DEST / name).write_bytes(data)
    return [{"file": name, "fixture_transform": "pinned baseline MP4 first AVC sample filler NAL, stsz/stco/mdat v1"}]


MIXED_CLOCK = {"video-late": (400, 0), "audio-late": (0, 400), "shifted": (600, 600)}


def mixed_clock_records(exe):
    # Fixed v0 edit-list transform. Keep media_time (AAC priming) and all
    # encoded samples unchanged; prepend an empty movie-time edit per track.
    original = (DEST / "baseline-aac.mp4").read_bytes()
    records = []
    for stem, (video_ms, audio_ms) in MIXED_CLOCK.items():
        data = bytearray(original)

        def u32(position):
            return int.from_bytes(data[position:position + 4], "big")

        def put(position, value):
            data[position:position + 4] = value.to_bytes(4, "big")

        def boxes(start, end):
            while start < end:
                size = u32(start)
                assert size >= 8 and start + size <= end
                yield start, size, bytes(data[start + 4:start + 8])
                start += size
            assert start == end

        def child(box, kind):
            start, size, unused = box
            return next(b for b in boxes(start + 8, start + size) if b[2] == kind)

        top = list(boxes(0, len(data)))
        moov = next(b for b in top if b[2] == b"moov")
        mdat = next(b for b in top if b[2] == b"mdat")
        mvhd = child(moov, b"mvhd")
        assert moov[0] < mdat[0] and data[mvhd[0] + 8] == 0 and u32(mvhd[0] + 20) == 1000
        insertions = []
        durations = []
        offsets = []
        for track in list(boxes(moov[0] + 8, moov[0] + moov[1])):
            if track[2] != b"trak":
                continue
            tkhd = child(track, b"tkhd")
            mdia = child(track, b"mdia")
            hdlr = child(mdia, b"hdlr")
            kind = bytes(data[hdlr[0] + 16:hdlr[0] + 20])
            assert kind in (b"vide", b"soun") and data[tkhd[0] + 8] == 0
            delay = video_ms if kind == b"vide" else audio_ms
            durations.append(u32(tkhd[0] + 28) + delay)
            put(tkhd[0] + 28, durations[-1])
            stbl = child(child(mdia, b"minf"), b"stbl")
            stco = child(stbl, b"stco")
            offsets += [stco[0] + 16 + index * 4 for index in range(u32(stco[0] + 12))]
            if not delay:
                continue
            edts = child(track, b"edts")
            elst = child(edts, b"elst")
            assert data[elst[0] + 8] == 0 and u32(elst[0] + 12) == 1 and elst[1] == 28
            put(elst[0] + 12, 2)
            for box in (track, edts, elst):
                put(box[0], box[1] + 12)
            insertions.append((elst[0] + 16, delay.to_bytes(4, "big") +
                               b"\xff\xff\xff\xff\x00\x01\x00\x00"))
        growth = 12 * len(insertions)
        assert len(durations) == 2 and growth in (12, 24)
        put(moov[0], moov[1] + growth)
        put(mvhd[0] + 24, max(durations))
        for position in offsets:
            assert u32(position) >= mdat[0] + 8
            put(position, u32(position) + growth)
        for position, payload in sorted(insertions, reverse=True):
            data[position:position] = payload
        name = "mixed-clock-" + stem + ".mp4"
        (DEST / name).write_bytes(data)
        records.append({"file": name, "fixture_transform": "pinned MP4 v0 empty edits, sizes/duration/stco v1",
                        "source": "baseline-aac.mp4", "video_delay_ms": video_ms, "audio_delay_ms": audio_ms})
    return records


MIXED_PTS = {"negative": (-1024, -1024, -1024), "backward": (4096, 0, 0),
             "gap": (0, 4096, 4096), "duplicate": (2048, 0, 0),
             "fallback": (0, -3072, 0)}


def mixed_pts_records(exe):
    # Fixed Baseline-only timing controls, not a general MP4 editor. Disable
    # only the video's edit list so negative presentation is not discarded.
    original = (DEST / "baseline-aac.mp4").read_bytes()
    records = []
    for stem, ctts in MIXED_PTS.items():
        data = bytearray(original)

        def u32(position):
            return int.from_bytes(data[position:position + 4], "big")

        def put(position, value):
            data[position:position + 4] = value.to_bytes(4, "big")

        def boxes(start, end):
            while start < end:
                size = u32(start)
                assert size >= 8 and start + size <= end
                yield start, size, bytes(data[start + 4:start + 8])
                start += size
            assert start == end

        def child(box, kind):
            return next(b for b in boxes(box[0] + 8, box[0] + box[1]) if b[2] == kind)

        top = list(boxes(0, len(data)))
        moov = next(b for b in top if b[2] == b"moov")
        mdat = next(b for b in top if b[2] == b"mdat")
        assert moov[0] < mdat[0]
        offsets = []
        video = None
        for track in boxes(moov[0] + 8, moov[0] + moov[1]):
            if track[2] != b"trak":
                continue
            mdia = child(track, b"mdia")
            stbl = child(child(mdia, b"minf"), b"stbl")
            stco = child(stbl, b"stco")
            offsets += [stco[0] + 16 + index * 4 for index in range(u32(stco[0] + 12))]
            hdlr = child(mdia, b"hdlr")
            if bytes(data[hdlr[0] + 16:hdlr[0] + 20]) == b"vide":
                assert video is None
                mdhd = child(mdia, b"mdhd")
                assert data[mdhd[0] + 8] == 0 and u32(mdhd[0] + 20) == 10240
                minf = child(mdia, b"minf")
                assert not any(b[2] == b"ctts" for b in boxes(stbl[0] + 8, stbl[0] + stbl[1]))
                edts = child(track, b"edts")
                data[edts[0] + 4:edts[0] + 8] = b"free"
                video = track, mdia, minf, stbl
                tkhd = child(track, b"tkhd")
                duration = max(600, max(index * 200 + value * 1000 // 10240
                                        for index, value in enumerate(ctts)) + 200)
                put(tkhd[0] + 28, duration)
        assert video is not None
        mvhd = child(moov, b"mvhd")
        put(mvhd[0] + 24, duration)
        payload = (40).to_bytes(4, "big") + b"ctts\x01\x00\x00\x00" + (3).to_bytes(4, "big")
        for value in ctts:
            payload += (1).to_bytes(4, "big") + value.to_bytes(4, "big", signed=True)
        for box in (moov,) + video:
            put(box[0], box[1] + len(payload))
        for position in offsets:
            assert u32(position) >= mdat[0] + 8
            put(position, u32(position) + len(payload))
        position = video[-1][0] + video[-1][1]
        data[position:position] = payload
        name = "mixed-pts-" + stem + ".mp4"
        (DEST / name).write_bytes(data)
        records.append({"file": name, "fixture_transform": "pinned MP4 video edts to free, ctts v1; sizes/duration/stco v1",
                        "source": "baseline-aac.mp4", "video_ctts_ticks": list(ctts), "video_timescale": 10240})
    return records


def extend(exe, group):
    check()
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    if hashlib.sha256(Path(exe).read_bytes()).hexdigest() != pin["generator_sha256"]:
        raise SystemExit("Extension requires the existing pinned generator")
    names, generator = {
        "mixed": ({"mixed-main-aac.mp4"}, mixed_records),
        "mixed-limit": ({"mixed-packet-limit.mp4"}, mixed_limit_records),
        "mixed-clock": ({"mixed-clock-" + stem + ".mp4" for stem in MIXED_CLOCK}, mixed_clock_records),
        "mixed-pts": ({"mixed-pts-" + stem + ".mp4" for stem in MIXED_PTS}, mixed_pts_records),
        "mixed-config": ({"mixed-config-" + stem + ".mp4" for stem in media_mixed_config_fixtures.CASES},
                         lambda unused: media_mixed_config_fixtures.records(DEST)),
        "mpeg": ({"mpeg2-mp2.ts", "mpeg1-mp2.mpg", "mpeg2-interlaced.ts", "mpeg2-oversize.ts"}, mpeg_records),
        "amr": ({"amr-nb.amr", "amr-wb.amr"}, amr_records),
        "ima": ({"ima-mono.wav", "ima-stereo.wav", "ima-mono.pcm", "ima-stereo.pcm"}, ima_records),
        "mpeg4": ({"mpeg4-simple.mp4", "mpeg4-asp-vga.mp4", "mpeg4-mp3.avi",
                   "mpeg4-interlaced.mp4", "mpeg4-oversize.mp4"}, mpeg4_records),
        "mjpeg": ({"mjpeg-mp3.avi", "mjpeg422.avi", "mp3-mono.mp3"}, extra_records),
        "h263": ({"h263-cif.avi", "h263p-vga.avi", "h263-oversize.avi"}, h263_records),
        "flv": ({"flv-h264-aac.flv", "flv-main-vga.flv", "flv-oversize.flv",
                 "flv1-unsupported.flv"}, flv_records),
        "flv-clock": ({"flv-clock-" + stem + ".flv" for stem in FLV_CLOCK}, flv_clock_records),
        "audio-change": ({"aac-lc-44100.aac", "aac-lc-stereo.aac"} |
                         {"aac-clock-" + stem + ".aac" for stem in AUDIO_CHANGE}, audio_change_records),
        "annexb": ({"annexb-" + stem + ".h264" for stem in
                    ["baseline-aac", "main-vga", "high", "high422", "interlaced", "oversize"]}, annexb_records),
        "annexb-sequence": ({"annexb-no-timing.h264", "annexb-repeat-sps.h264", "annexb-change-no-timing.h264"} |
                            {"annexb-change-" + stem + ".h264" for stem in
                             ["main-vga", "high", "high422", "interlaced", "oversize"]}, annexb_sequence_records),
        "annexb-damage": ({"annexb-" + stem + ".h264" for stem in ANNEXB_DAMAGE}, annexb_damage_records),
        "annexb-parameters": ({"annexb-" + stem + ".h264" for stem in ANNEXB_PARAMETERS}, annexb_parameter_records),
    }[group]
    if any(r["file"] in names for r in pin["files"]):
        raise SystemExit("Already extended; use --generate for intentional full regeneration")
    write_pin(exe, pin["files"] + generator(exe))


def generate(exe):
    DEST.mkdir(parents=True, exist_ok=True)
    records = []
    cases = [
        ("baseline-aac.mp4", "320x240", "baseline", "yuv420p", "bframes=0", True),
        ("main-vga.mp4", "640x480", "main", "yuv420p", "bframes=2:8x8dct=0", False),
        ("high.mp4", "320x240", "high", "yuv420p", "8x8dct=1", False),
        ("high422.mp4", "320x240", "high422", "yuv422p", "8x8dct=1", False),
        ("interlaced.mp4", "320x240", "main", "yuv420p", "tff=1:8x8dct=0", False),
        ("oversize.mp4", "656x480", "baseline", "yuv420p", "bframes=0", False),
    ]
    for name, size, profile, pixels, params, audio in cases:
        args = ["-f", "lavfi", "-i", f"color=c=red:s={size}:r=5:d=0.6"]
        if audio:
            args += ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6"]
        args += ["-map", "0:v", "-c:v", "libx264", "-threads:v", "1", "-preset", "medium",
                 "-profile:v", profile, "-pix_fmt", pixels, "-crf", "18",
                 "-x264-params", f"{params}:keyint=5:scenecut=0"]
        if audio:
            args += ["-map", "1:a", "-c:a", "aac", "-profile:a", "aac_low", "-ac", "2",
                     "-b:a", "64k", "-threads:a", "1"]
        else:
            args += ["-an"]
        args += ["-t", "0.6", "-fflags", "+bitexact", "-flags:v", "+bitexact",
                 "-map_metadata", "-1", "-movflags", "+faststart", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    for name, profile in [("aac-lc.aac", "aac_low"), ("aac-main.aac", "aac_main")]:
        args = ["-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=0.6",
                "-c:a", "aac", "-profile:a", profile, "-ac", "1", "-b:a", "64k",
                "-threads", "1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
                "-f", "adts", "-y", str(DEST / name)]
        run(exe, args)
        records.append({"file": name, "arguments": args[:-1] + [name]})
    write_pin(exe, records + extra_records(exe) + mpeg_records(exe) + amr_records(exe) +
              ima_records(exe) + mpeg4_records(exe) + h263_records(exe) + flv_records(exe) +
              annexb_records(exe) + annexb_sequence_records(exe) + annexb_damage_records(exe) + annexb_parameter_records(exe) + flv_clock_records(exe) + audio_change_records(exe) + mixed_records(exe) + mixed_limit_records(exe) + mixed_clock_records(exe) + mixed_pts_records(exe) + media_mixed_config_fixtures.records(DEST))


def check():
    manifest = json.loads(PIN.read_text(encoding="utf-8"))
    for record in manifest["files"]:
        data = (DEST / record["file"]).read_bytes()
        assert len(data) == record["bytes"], record["file"]
        assert hashlib.sha256(data).hexdigest() == record["sha256"], record["file"]
        print(f"OK {record['file']}: {len(data)} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--generate", action="store_true")
    action.add_argument("--extend", action="store_true")
    action.add_argument("--extend-mpeg", action="store_true")
    action.add_argument("--extend-amr", action="store_true")
    action.add_argument("--extend-ima", action="store_true")
    action.add_argument("--extend-mpeg4", action="store_true")
    action.add_argument("--extend-h263", action="store_true")
    action.add_argument("--extend-flv", action="store_true")
    action.add_argument("--extend-flv-clock", action="store_true")
    action.add_argument("--extend-audio-change", action="store_true")
    action.add_argument("--extend-mixed", action="store_true")
    action.add_argument("--extend-mixed-limit", action="store_true")
    action.add_argument("--extend-mixed-clock", action="store_true")
    action.add_argument("--extend-mixed-pts", action="store_true")
    action.add_argument("--extend-mixed-config", action="store_true")
    action.add_argument("--extend-annexb", action="store_true")
    action.add_argument("--extend-annexb-sequence", action="store_true")
    action.add_argument("--extend-annexb-damage", action="store_true")
    action.add_argument("--extend-annexb-parameters", action="store_true")
    parser.add_argument("--ffmpeg")
    opts = parser.parse_args()
    if opts.generate or opts.extend or opts.extend_mpeg or opts.extend_amr or opts.extend_ima or opts.extend_mpeg4 or opts.extend_h263 or opts.extend_flv or opts.extend_flv_clock or opts.extend_audio_change or opts.extend_mixed or opts.extend_mixed_limit or opts.extend_mixed_clock or opts.extend_mixed_pts or opts.extend_mixed_config or opts.extend_annexb or opts.extend_annexb_sequence or opts.extend_annexb_damage or opts.extend_annexb_parameters:
        if not opts.ffmpeg:
            parser.error("Generation/extension requires an explicit --ffmpeg path")
        if opts.extend or opts.extend_mpeg or opts.extend_amr or opts.extend_ima or opts.extend_mpeg4 or opts.extend_h263 or opts.extend_flv or opts.extend_flv_clock or opts.extend_audio_change or opts.extend_mixed or opts.extend_mixed_limit or opts.extend_mixed_clock or opts.extend_mixed_pts or opts.extend_mixed_config or opts.extend_annexb or opts.extend_annexb_sequence or opts.extend_annexb_damage or opts.extend_annexb_parameters:
            extend(opts.ffmpeg, "mixed-config" if opts.extend_mixed_config else "mixed-pts" if opts.extend_mixed_pts else "mixed-clock" if opts.extend_mixed_clock else "mixed-limit" if opts.extend_mixed_limit else "mixed" if opts.extend_mixed else "audio-change" if opts.extend_audio_change else "flv-clock" if opts.extend_flv_clock else "annexb-parameters" if opts.extend_annexb_parameters else "annexb-damage" if opts.extend_annexb_damage else "annexb-sequence" if opts.extend_annexb_sequence else "annexb" if opts.extend_annexb else "flv" if opts.extend_flv else "h263" if opts.extend_h263 else "mpeg4" if opts.extend_mpeg4 else "ima" if opts.extend_ima else "amr" if opts.extend_amr else
                   "mpeg" if opts.extend_mpeg else "mjpeg")
        else:
            generate(opts.ffmpeg)
    check()
