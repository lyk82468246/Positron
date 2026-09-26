# Positron FFmpeg 3.4.14 pin

This directory is the FFmpeg 3.4.14 release source snapshot used by the
ARMV4I software-decoder backend. The upstream sources are unchanged; the
two small source edits needed by the VS2008 C89 conversion are recorded as
patches under `positron_patches/`.

- Source archive: `https://ffmpeg.org/releases/ffmpeg-3.4.14.tar.gz`
- SHA-256: `61E255E824341E62FBC83365D84EB754FA558F53985D28AD610D13AEEE895D5A`
- Release tag: `n3.4.14`
- License files: `LICENSE.md`, `COPYING.GPLv2`, `COPYING.LGPLv2.1` and the
  other upstream `COPYING.*` files in this directory.

The WM6 build uses a generated and reviewed 255-object source subset rather
than compiling the FFmpeg command-line tools or the whole source tree. The
subset is `libavcodec`, `libavformat` and `libavutil` with a custom memory AVIO
only: no network protocols, no muxers, no encoder registration, no filters,
no multi-threading, no runtime CPU dispatch and no ARMv5/ARMv6/VFP/NEON code.
The enabled decoder/demuxer contract is H.264, MPEG-4 Part 2, MPEG-1/2 Video,
MJPEG, H.263, AAC-LC, MP2/MP3, AMR-NB/WB, PCM and IMA ADPCM in AVI, MP4/MOV,
MPEG-PS, MPEG-TS, FLV, WAV and selected raw streams. AV1, HEVC and VP9 are
not compiled into the contract.

The normal VS2008 project links the fixed ARMV4I archive
`positron_ffmpeg_armv4i.lib`; its SHA-256 is
`9a0615f9ceee423145a3495466511197af6385f61db92e8a212f80b117069797`.
The archive is intentionally checked in even though the repository-wide
`*.lib` ignore rule covers ordinary build outputs. It was produced offline
with the VideoLAN `c99-to-c89` 1.0.3 tools (package SHA-256
`5a060722777fe66253add5ee07316a48de1d22d7f73268ad10e4452ce2d7d556`),
the fixed source archive above, the two recorded patches and the WM6
compatibility headers in `compat/`. Formal project builds never download
these inputs.

To regenerate the archive on a machine with VS2008, the WM6 SDK and the
matching `c99conv.exe`, set `C99CONV` to that executable and run
`scripts\\build_ffmpeg_armv4i.bat`. The script uses
`positron_sources.txt`, copies the source into ignored `tmp/` build space,
applies the recorded patches there, and leaves the rebuilt archive at this
path. It never edits the vendored source tree and does not download tools.

The public capability probe is backed by the linked decoder set, not by a
desktop FFmpeg installation. The FFmpeg wrapper keeps the input in a bounded
memory copy, emits only I420 video and interleaved S16LE audio synchronously,
and enforces the public 640x480/default two-channel limits. H.264 profiles
outside Baseline/Main, non-8-bit/non-4:2:0 video and unsupported sample
layouts fail closed at open or decode time.
