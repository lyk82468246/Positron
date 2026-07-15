# positron_image_demo

Minimal Windows Mobile 6 C consumer for `positron_image.dll`.

The project links the product library `positron_image.lib` plus the Windows
Mobile system shell library `aygshell.lib`; it does not include or link
`positron_core`, NetSurf, libdom, or `test_host`. At startup it accepts a
compatible ABI (the same major and a sufficient minor), creates retained
bitmaps from padded top-down BGR24 and straight-alpha BGRA32 rows, clears the
caller buffers, encodes RGB and alpha PNG through native WM Imaging and a
quality-100 4:4:4 JPEG through the bundled libjpeg-turbo compressor, decodes
those results after freeing the encoded buffers, and also draws a retained SVG
through the public C API. A deliberately short pixel buffer must be rejected
before the visible sample starts. ABI 1.4 also performs native WM Imaging BMP
and GIF encode/signature/decode checks before creating the window.

Build with the root solution, then stage with:

```cmd
scripts\stage_image_demo.bat Debug C:\WMShare\Positron-next52
```

Expected display: six responsive cells in portrait or landscape: saturated
raw BGR24, lighter half-alpha raw BGRA32, saturated RGB PNG, matching lighter
alpha PNG, quality-100 JPEG 4:4:4 and the retained red/green/blue SVG. The demo
parses the JPEG SOF marker and refuses to start unless all three sampling
factors are 1x1. Any length, stride, copy lifetime, alpha, signature, sampling,
re-decode or drawing failure produces an error message or a visible mismatch.
The native Windows Mobile title-bar OK button is enabled with
`SHDoneButton(SHDB_SHOW)`. Its `WM_COMMAND/IDOK` destroys the window and ends
the process; next52 device testing confirmed the process disappears and the
demo can be launched again. The shell's X is Smart Minimize and does not promise `WM_CLOSE`,
which is why next51 remained in the Running Programs list. No softkey bar is
added, so the image client area remains unchanged.
