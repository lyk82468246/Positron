# NanoSVG rasterizer upstream

- Project: NanoSVG
- Repository: https://github.com/memononen/nanosvg
- Commit: `9da543e8329fdd81b64eb48742d8ccb09377aed1`
- License: zlib; the notice is embedded at the top of both vendored headers.

Vendored files and SHA-256:

- `nanosvg.h`: `B0057538CD65B6D8A37A2EF7F5AA3905A14F82964B741534CA61BF20054DCC31`
- `nanosvgrast.h`: `A214064976096E50EFFCD38E00452FBBBFE6D8430D146FC0C324BCEB4DF693C7`

Positron does not use NanoSVG's parser. libsvgtiny remains the only SVG
parser; parsed paths are adapted to NanoSVG's mature CPU rasterizer for
anti-aliased fill and stroke output.
