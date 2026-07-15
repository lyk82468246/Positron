# positron_image_demo

Minimal Windows Mobile 6 C consumer for `positron_image.dll`.

The project links only `positron_image.lib`; it does not include or link
`positron_core`, NetSurf, libdom, or `test_host`. At startup it accepts a
compatible ABI (the same major and a sufficient minor), creates one retained
WM bitmap, encodes it to PNG through native WM Imaging and to quality-100,
4:4:4 JPEG through the bundled libjpeg-turbo compressor,
decodes both results again after freeing those buffers, and also draws a
retained SVG through the public C API.

Build with the root solution, then stage with:

```cmd
scripts\stage_image_demo.bat Debug C:\WMShare\Positron-next49
```

Expected display: source BMP, lossless PNG round-trip and quality-100 4:4:4
JPEG round-trip four-colour squares plus red/green SVG blocks with a blue
curve. The demo parses the JPEG SOF marker and refuses to start unless all
three sampling factors are 1x1. The JPEG may retain tiny DCT edge noise, but
should not show the broad purple/yellow bands seen in next47/48. Any API,
signature, sampling, re-decode or drawing failure produces an error message.
