# positron_media.dll

`positron_media.dll` is the WM6 media boundary.  The public header is a
stable C ABI: input is supplied by synchronous UTF-8-independent byte-stream
callbacks, output is delivered as borrowed video/PCM callback buffers, and a
session is driven by `pm_pump()`.

The first checked-in vertical is intentionally bounded:

- WAV PCM (8/16-bit, mono/stereo) and IMA ADPCM are decoded in portable C;
- the FFmpeg 3.4.14 ARMV4I archive is linked into the DLL through a custom
  memory AVIO path. It covers AVI, MP4/MOV, MPEG-PS, MPEG-TS, FLV, WAV and
  selected raw streams, with H.264/AVC, MPEG-4 Part 2, MPEG-1/2 Video, MJPEG,
  H.263, AAC-LC, MP2/MP3, AMR-NB/WB, PCM and IMA ADPCM decoders;
- for a WAV PCM stream, `AUTO` first opens the device WaveOut path and exposes
  the same synchronous S16LE callback while the device consumes each borrowed
  block. If the device rejects that format, `AUTO` falls back to the soft
  path. The WM6 DirectShow graph is also probed without exposing COM types,
  but a callback-backed DirectShow source filter and native video renderer
  lifecycle are not claimed yet; `PMEDIA_BACKEND_NATIVE` therefore remains
  limited to the WaveOut PCM case and fails closed for other inputs;
- software video output is I420/YUV420P, 8-bit, progressive and bounded by
  the requested maximum (640x480 by default). Audio output is interleaved
  S16LE through synchronous callbacks.

`pm_probe()` reports the demuxer/decoder set actually linked into this DLL;
device DirectShow/ACM/WaveOut availability remains runtime-only and is not a
desktop format guarantee. No network access, long-lived DLL worker thread,
encoder, DRM, subtitle renderer, AV1, HEVC or VP9 path is part of this DLL.
The input is retained in a bounded 16 MiB memory copy, so the soft backend
does not require a seekable host source, but `WOULD_BLOCK` during open is a
hard error rather than an asynchronous wait.

The FFmpeg source is GPLv2-or-later/LGPLv2.1-or-later depending on the
selected files and is retained with its upstream notices.  The vendored
snapshot and ARMV4I port boundary are documented in
`third_party/ffmpeg-3.4.14/POSITRON_PORT.md` and `THIRD_PARTY.md`.
