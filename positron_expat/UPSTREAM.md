# Expat upstream

- Project: Expat XML Parser
- Version: 2.8.2
- Tag: `R_2_8_2`
- Release date: 2026-06-25
- Source: https://github.com/libexpat/libexpat/releases/tag/R_2_8_2
- License: MIT; see `COPYING`

The `expat/` directory contains the upstream library files needed by the
three official static-library translation units: `xmlparse.c`, `xmlrole.c`
and `xmltok.c`. Positron adds `expat_config.h` for the WM6 build and
`random_wince.c` as the platform entropy adapter.

`scripts/port_expat_vs2008.py` applies the version-locked C++ compatibility
layer to `xmlparse.c`. The implementation is compiled as C++ because Expat
2.8.2 requires C99, while its public API remains the upstream C ABI.

Build profile:

- UTF-8 API
- namespace processing enabled
- DTD and user-defined general entities disabled
- parse context retention disabled
- static-library ABI (`XML_STATIC`)

This profile is sufficient for libdom/libsvgtiny SVG parsing and reduces the
memory and untrusted-XML attack surface on Windows Mobile.
