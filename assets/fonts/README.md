# Positron bundled fallback fonts

`PositronSymbols.ttf` and `PositronEmoji.ttf` are generated static TrueType
subsets for the Windows Mobile GDI backend. Run:

```text
python scripts/build_fonts.py
```

The symbols source is Noto Sans Symbols 2 from `notofonts/symbols` commit
`635cef9`. The emoji source is Noto Emoji from the Google Fonts repository.
The build fixes Noto Emoji at weight 400, removes its variable-font machinery,
subsets both inputs, and renames the derived font families as required for a
modified OFL font.

Both source fonts and these derived files are licensed under SIL Open Font
License 1.1. The corresponding license texts and unmodified source files are
under `third_party/noto-symbols2` and `third_party/noto-emoji`.

These are monochrome outline fonts. They deliberately do not use CBDT/CBLC,
COLR/CPAL, SVG-in-OpenType, or variable-font tables that the WM6 GDI font
engine cannot be expected to understand.
