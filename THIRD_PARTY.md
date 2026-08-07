# Third-party source and licenses

Positron keeps the source dependencies required by `Positron.sln` in this
repository. A normal clone therefore needs no source download before it can be
built. Visual Studio 2008 SP1 and the Windows Mobile 6 Professional SDK remain
external proprietary toolchain requirements and are not redistributed here.

The root `LICENSE` applies only to original Positron code. Vendored files keep
their upstream licenses and notices; the repository does not relicense them.
In particular, code compiled from NetSurf's browser directory is GPLv2, so a
binary containing that code must be distributed in compliance with GPLv2.

| Component | Pinned source | Repository path | License / notice |
|---|---|---|---|
| NetSurf complete source bundle | NetSurf 3.11 | `netsurf-all-3.11/` | Browser sources under `netsurf/` are GPLv2; artwork is MIT. Support libraries have their own `COPYING` files and are predominantly MIT. See `netsurf-all-3.11/netsurf/COPYING` and each component's `COPYING`. |
| Mbed TLS | tag `mbedtls-2.16.12`, commit `cf4667126010c665341f9e50ef691b7ef8294188` | `positron_tls/mbedtls/` | Dual Apache-2.0 OR GPL-2.0-or-later. Positron selects Apache-2.0. This is a legacy WM6/MSVC9 compatibility pin, not a claim of current upstream support; review current security advisories before shipping. See `LICENSE` and `apache-2.0.txt` in that directory. |
| cJSON | tag `v1.7.18`, commit `acc76239bee01d8e9c858ae2cab296704e52d916` | `positron_json/cjson/` | MIT; see `positron_json/cjson/LICENSE`. |
| Expat | tag `R_2_8_2` | `positron_expat/expat/` | MIT; see `positron_expat/COPYING` and `positron_expat/UPSTREAM.md`. |
| Duktape | 2.7.0, source bundled by NetSurf 3.11 | `netsurf-all-3.11/netsurf/content/handlers/javascript/duktape/` | Duktape itself is MIT; the upstream license and copyright notice are embedded in `duktape.c`/`duktape.h`. The build also includes NetSurf's GPLv2 `duk_custom.h` configuration layer, so a binary containing this combined source must follow the applicable GPLv2 obligations. `positron_script.dll` keeps the Duktape ABI private. |
| libjpeg-turbo | 1.5.3, commit `bf6c774305c9feb30cff7b99e1a475df61bfa008` | `third_party/libjpeg-turbo/` | IJG, modified BSD and zlib terms; see `LICENSE.md`, `README.ijg` and `POSITRON_PORT.md`. Binary documentation must retain the IJG attribution recorded there. |
| NanoSVG rasterizer | commit `9da543e8329fdd81b64eb48742d8ccb09377aed1` | `positron_image/third_party/nanosvg/` | zlib notice embedded in both headers; see `UPSTREAM.md`. |
| Noto Sans Symbols / Symbols 2 / Noto Emoji | pinned files documented beside each source | `third_party/noto-symbols*`, `third_party/noto-emoji/` | SIL Open Font License 1.1. Each directory contains `OFL.txt`; generated renamed subsets are under `assets/fonts/`. |
| tiny-regex-c | commit `f2632c6d9ed25272987471cdb8b70395c2460bdb` | `third_party/tiny-regex-c/` | Public domain / Unlicense; see `LICENSE` and `UPSTREAM.md`. Used only by the C89 `pattern` validity adapter. |
| Mozilla CA trust data via curl | 121-certificate snapshot generated on 2026-06-10 | `positron_tls/ca_bundle.h` | Certificate trust data generated from `https://curl.se/ca/cacert.pem`; update with `positron_tls/gen_ca_bundle.py`. This is data, not an additional linked code library. |

## Vendoring policy

- Keep the exact upstream license and copyright notices with every vendored
  component.
- Record the tag or commit and any Positron changes in an `UPSTREAM.md` or
  equivalent port note.
- Prefer a pinned, reviewable source snapshot over a build-time download.
- Do not commit Microsoft SDK, Visual Studio, emulator or device images; their
  redistribution terms are separate from this repository.
- Run `python scripts/audit_repo.py` after changing projects or dependencies.
  It verifies project inputs, version pins, license files and Git tracking.

This file is an engineering inventory, not legal advice. Distributors remain
responsible for satisfying the licenses of the exact source and binaries they
ship.
