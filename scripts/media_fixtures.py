"""Offline procedural media fixtures; never run by the product build.

Use --generate --ffmpeg PATH only to intentionally replace the corpus/pin.
The default mode verifies committed byte counts and SHA-256 without FFmpeg.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

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


def extend(exe, group):
    check()
    pin = json.loads(PIN.read_text(encoding="utf-8"))
    if hashlib.sha256(Path(exe).read_bytes()).hexdigest() != pin["generator_sha256"]:
        raise SystemExit("Extension requires the existing pinned generator")
    names, generator = {
        "mpeg": ({"mpeg2-mp2.ts", "mpeg1-mp2.mpg", "mpeg2-interlaced.ts", "mpeg2-oversize.ts"}, mpeg_records),
        "amr": ({"amr-nb.amr", "amr-wb.amr"}, amr_records),
        "ima": ({"ima-mono.wav", "ima-stereo.wav", "ima-mono.pcm", "ima-stereo.pcm"}, ima_records),
        "mpeg4": ({"mpeg4-simple.mp4", "mpeg4-asp-vga.mp4", "mpeg4-mp3.avi",
                   "mpeg4-interlaced.mp4", "mpeg4-oversize.mp4"}, mpeg4_records),
        "mjpeg": ({"mjpeg-mp3.avi", "mjpeg422.avi", "mp3-mono.mp3"}, extra_records),
        "h263": ({"h263-cif.avi", "h263p-vga.avi", "h263-oversize.avi"}, h263_records),
        "flv": ({"flv-h264-aac.flv", "flv-main-vga.flv", "flv-oversize.flv",
                 "flv1-unsupported.flv"}, flv_records),
        "annexb": ({"annexb-" + stem + ".h264" for stem in
                    ["baseline-aac", "main-vga", "high", "high422", "interlaced", "oversize"]}, annexb_records),
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
              ima_records(exe) + mpeg4_records(exe) + h263_records(exe) + flv_records(exe) + annexb_records(exe))


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
    action.add_argument("--extend-annexb", action="store_true")
    parser.add_argument("--ffmpeg")
    opts = parser.parse_args()
    if opts.generate or opts.extend or opts.extend_mpeg or opts.extend_amr or opts.extend_ima or opts.extend_mpeg4 or opts.extend_h263 or opts.extend_flv or opts.extend_annexb:
        if not opts.ffmpeg:
            parser.error("Generation/extension requires an explicit --ffmpeg path")
        if opts.extend or opts.extend_mpeg or opts.extend_amr or opts.extend_ima or opts.extend_mpeg4 or opts.extend_h263 or opts.extend_flv or opts.extend_annexb:
            extend(opts.ffmpeg, "annexb" if opts.extend_annexb else "flv" if opts.extend_flv else "h263" if opts.extend_h263 else "mpeg4" if opts.extend_mpeg4 else "ima" if opts.extend_ima else "amr" if opts.extend_amr else
                   "mpeg" if opts.extend_mpeg else "mjpeg")
        else:
            generate(opts.ffmpeg)
    check()
