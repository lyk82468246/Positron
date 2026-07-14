# positron_image_demo

Minimal Windows Mobile 6 C consumer for `positron_image.dll`.

The project links only `positron_image.lib`; it does not include or link
`positron_core`, NetSurf, libdom, or `test_host`. At startup it accepts a
compatible ABI (the same major and a sufficient minor), creates one retained
WM bitmap, encodes it to PNG and JPEG memory buffers through native WM Imaging,
decodes both results again after freeing those buffers, and also draws a
retained SVG through the public C API.

Build with the root solution, then stage with:

```cmd
scripts\stage_image_demo.bat Debug C:\WMShare\Positron-next47
```

Expected display: source BMP, lossless PNG round-trip and JPEG round-trip
four-colour squares plus red/green SVG blocks with a blue curve. Any API,
signature, re-decode or drawing failure produces an error message instead.
