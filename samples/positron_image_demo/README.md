# positron_image_demo

Minimal Windows Mobile 6 C consumer for `positron_image.dll`.

The project links only `positron_image.lib`; it does not include or link
`positron_core`, NetSurf, libdom, or `test_host`. At startup it accepts a
compatible ABI (the same major and a sufficient minor),
creates one retained WM bitmap and one retained SVG, verifies their intrinsic
sizes, and draws both through the public C API.

Build with the root solution, then stage with:

```cmd
scripts\stage_image_demo.bat Debug C:\WMShare\Positron-next46
```

Expected display: a four-colour square on the left and red/green blocks with a
smooth blue curve on the right. Any API or drawing failure produces an error
message instead.
