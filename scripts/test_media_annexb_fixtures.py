"""Read-only pinned Annex-B oracle; CLI-generated PTS are not WM6 PTS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

import media_fixtures
from test_media_mpeg4_fixtures import decode, require


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    opts = parser.parse_args()
    media_fixtures.check()
    pin = json.loads(media_fixtures.PIN.read_text(encoding="utf-8"))
    require(hashlib.sha256(Path(opts.ffmpeg).read_bytes()).hexdigest() == pin["generator_sha256"],
            "Use the pinned generator")
    for stem, width, height, pixels, kinds, fields in [
        ("baseline-aac", 320, 240, "yuv420p", "IPP", "PPP"),
        ("main-vga", 640, 480, "yuv420p", "IBP", "PPP"),
        ("high", 320, 240, "yuv420p", "IBP", "PPP"),
        ("high422", 320, 240, "yuv422p", "IBP", "PPP"),
        ("interlaced", 320, 240, "yuv420p", "IBP", "BBB"),
        ("oversize", 656, 480, "yuv420p", "IPP", "PPP"),
        ("no-timing", 320, 240, "yuv420p", "IPP", "PPP"),
        ("repeat-sps", 320, 240, "yuv420p", "IPPIPP", "PPPPPP"),
        ("repeat-pps", 320, 240, "yuv420p", "IPP", "PPP"),
        ("pps-id1", 320, 240, "yuv420p", "IPP", "PPP"),
        ("change-pps", 320, 240, "yuv420p", "IPPIPP", "PPPPPP"),
    ]:
        name = "annexb-" + stem + ".h264"
        raw, log = decode(opts.ffmpeg, name, ["-debug_ts", "-map", "0:v:0", "-vf", "showinfo",
                          "-fps_mode", "passthrough", "-c:v", "rawvideo", "-pix_fmt", pixels,
                          "-threads:v", "1", "-f", "rawvideo", "pipe:1"])
        frames = re.findall(r"n:\s*\d+\s+pts:.*?fmt:(\w+).*?\bs:(\d+)x(\d+)"
                            r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)", log)
        count = len(kinds)
        require(len(frames) == count, name + ": decoded frame count")
        require(log.count("demuxer ->") == count and
                all("pkt_pts:NOPTS" in line and "pkt_dts:NOPTS" in line
                    for line in log.splitlines() if "demuxer ->" in line), name + ": unknown packet PTS")
        plane_counts = [width * height, width * height // (2 if pixels == "yuv422p" else 4)]
        require(len(raw) == (plane_counts[0] + 2 * plane_counts[1]) * count, name + ": bytes")
        offset = 0
        for index, frame in enumerate(frames):
            require(frame == (pixels, str(width), str(height), fields[index],
                              str(int(index % 3 == 0)), kinds[index]), name + ": frame properties")
            for count, target in [(plane_counts[0], 81), (plane_counts[1], 90), (plane_counts[1], 240)]:
                plane = raw[offset:offset + count]
                require(min(plane) >= target - 1 and max(plane) <= target + 1, name + ": pixels")
                offset += count
        data = (media_fixtures.DEST / name).read_bytes()
        units = re.split(b"\x00\x00\x00?\x01", data)[1:]
        require({7, 8, 5}.issubset({unit[0] & 31 for unit in units if unit}), name + ": SPS/PPS/IDR")
        print("ORACLE", name, width, height, pixels, kinds, "packet PTS/DTS unknown")
    _, log = decode(opts.ffmpeg, "annexb-no-timing.h264",
                    ["-c:v", "copy", "-bsf:v", "trace_headers", "-f", "null", "pipe:1"])
    require("timing_info_present_flag" in log and
            all("= 0" in line for line in log.splitlines() if "timing_info_present_flag" in line) and
            "num_units_in_tick" not in log and "time_scale" not in log, "Removed SPS timing")
    for stem, width, height, pixels, kinds, fields in [
        ("no-timing", 320, 240, "yuv420p", "IPP", "PPP"),
        ("main-vga", 640, 480, "yuv420p", "IBP", "PPP"),
        ("high", 320, 240, "yuv420p", "IBP", "PPP"),
        ("high422", 320, 240, "yuv422p", "IBP", "PPP"),
        ("interlaced", 320, 240, "yuv420p", "IBP", "BBB"),
        ("oversize", 656, 480, "yuv420p", "IPP", "PPP"),
    ]:
        name = "annexb-change-" + stem + ".h264"
        _, log = decode(opts.ffmpeg, name, ["-vf", "showinfo", "-fps_mode", "passthrough",
                                           "-threads:v", "1", "-f", "null", "pipe:1"])
        frames = re.findall(r"n:\s*\d+\s+pts:.*?fmt:(\w+).*?\bs:(\d+)x(\d+)"
                            r"\s+i:(\w)\s+iskey:(\d)\s+type:(\w)", log)
        expected = [("yuv420p", "320", "240", "P", str(int(i == 0)), kind)
                    for i, kind in enumerate("IPP")]
        expected += [(pixels, str(width), str(height), fields[i], str(int(i == 0)), kind)
                     for i, kind in enumerate(kinds)]
        require(frames == expected, name + ": six frames across a valid SPS change")
        units = re.split(b"\x00\x00\x00?\x01", (media_fixtures.DEST / name).read_bytes())[1:]
        sps = [unit.rstrip(b"\x00") for unit in units if unit and unit[0] & 31 == 7]
        require(len(sps) == 2 and sps[0] != sps[1], name + ": distinct SPS")
        print("ORACLE", name, "six decoded frames; different SPS; WM6 must reject before output")
    for stem in media_fixtures.ANNEXB_DAMAGE + ["dangling-pps", "late-dangling-pps", "pps-missing-sps"]:
        name = "annexb-" + stem + ".h264"
        flags = (subprocess.BELOW_NORMAL_PRIORITY_CLASS | subprocess.CREATE_NO_WINDOW) if os.name == "nt" else 0
        result = subprocess.run([opts.ffmpeg, "-hide_banner", "-nostdin", "-xerror",
                                 "-err_detect", "bitstream+buffer+explode", "-threads", "1",
                                 "-f", "h264", "-i", str(media_fixtures.DEST / name),
                                 "-vf", "showinfo", "-fps_mode", "passthrough", "-threads:v", "1",
                                 "-f", "null", "pipe:1"], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=30, creationflags=flags)
        log = result.stderr.decode("utf-8", errors="replace")
        if stem == "two-frames":
            require(result.returncode == 0 and len(re.findall(r"n:\s*\d+\s+pts:", log)) == 2,
                    name + ": clean EOF at a complete NAL boundary")
        else:
            require(result.returncode != 0, name + ": strict desktop decoder must detect damage")
        print("ORACLE", name, "clean two-frame EOF" if stem == "two-frames" else "strict decode rejected")
    for stem in ["repeat-pps", "change-pps", "pps-id1"]:
        _, log = decode(opts.ffmpeg, "annexb-" + stem + ".h264",
                        ["-c:v", "copy", "-bsf:v", "trace_headers", "-f", "null", "pipe:1"])
        values = [line for line in log.splitlines() if "pic_parameter_set_id" in line]
        require(values and all("= " + str(int(stem == "pps-id1")) in line for line in values),
                stem + ": PPS/slice identity")
        if stem == "change-pps":
            flags = [line for line in log.splitlines() if "constrained_intra_pred_flag" in line]
            require(any("= 0" in line for line in flags) and any("= 1" in line for line in flags),
                    "Valid PPS change must modify constrained_intra_pred_flag")
    for stem in ["sps-4096", "sps-4097", "pps-4096", "pps-4097", "late-pps-4097"]:
        units = re.split(b"\x00\x00\x00?\x01", (media_fixtures.DEST / ("annexb-" + stem + ".h264")).read_bytes())[1:]
        damaged = [u for u in units if len(u) >= 4096 and u[0] & 31 in (7, 8)]
        require(len(damaged) == 1 and len(damaged[0]) == (4096 if stem.endswith("4096") else 4097),
                stem + ": exact NAL byte budget, no trailing zero normalization")
        bits = "".join(f"{b:08b}" for b in damaged[0])
        offset = 32 if stem.startswith("sps") else 8
        zeros = bits.index("1", offset) - offset
        end = offset + zeros * 2 + 1
        identity = int(bits[offset + zeros:end], 2) - 1
        require(identity == (32 if stem.startswith("sps") else 256), stem + ": invalid identity")
        if not stem.startswith("sps"):
            require(bits[end] == "1", stem + ": references the existing SPS 0")
    units = re.split(b"\x00\x00\x00?\x01", (media_fixtures.DEST / "annexb-slice-before-pps.h264").read_bytes())[1:]
    kinds = [u[0] & 31 for u in units if u]
    require(kinds.index(5) < kinds.index(8), "PPS occurs after the first IDR slice")
    print("Parameter budget/order oracle PASS; invalid-ID capacity inputs do not prove valid maximum-size parameters.")
    print("Annex-B oracle PASS; CLI synthesized frame PTS do not define the DLL contract.")


if __name__ == "__main__":
    main()
