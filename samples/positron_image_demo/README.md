# `positron_image_demo`

Minimal Windows Mobile 6 consumer of the public `positron_image.dll` C API.
It does not include or link `positron_core`, NetSurf, libdom or `test_host`.

## What it demonstrates

- ABI-major/minor compatibility checking;
- retained bitmaps from padded top-down BGR24 pixels;
- retained bitmaps from straight-alpha BGRA32 pixels;
- PNG, JPEG, BMP and GIF encode/decode through public APIs;
- quality-100 4:4:4 JPEG output through the bundled libjpeg-turbo backend;
- retained SVG drawing;
- caller-buffer lifetime, stride and short-buffer validation;
- repeated drawing after input and encoded buffers have been released.

The project links `positron_image.lib` and the WM shell library `aygshell.lib`.

## Build and stage

Build the root solution, then run:

```bat
scripts\stage_image_demo.bat Debug C:\WMShare\Positron-image-demo
```

Configure the emulator shared folder and launch the staged executable from its
Storage Card path.

## Expected result

The window shows six cells in portrait and landscape:

1. saturated raw BGR24;
2. lighter half-alpha raw BGRA32;
3. saturated RGB PNG;
4. matching lighter alpha PNG;
5. quality-100 JPEG 4:4:4;
6. red/green/blue retained SVG.

The demo parses the JPEG SOF marker and refuses to start unless all three
sampling factors are 1x1. Length, stride, copy lifetime, alpha, signature,
sampling, re-decode or drawing failures produce an error or visible mismatch.

Use the native title-bar OK button to destroy the window and end the process.
The shell X button performs Smart Minimize on WM6 and does not guarantee
`WM_CLOSE`.

Public ownership and thread-affinity rules are defined in
[`../../positron_image/positron_image.h`](../../positron_image/positron_image.h).
