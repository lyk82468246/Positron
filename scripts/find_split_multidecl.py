#!/usr/bin/env python3
"""find_split_multidecl.py - locate multi-declarator declarations that c89ize.py
left in place AND that now sit after a statement, which MSVC's C89 compiler
rejects (error C2143 "missing ';' before type").

Background: c89ize.py hoists single-declarator block declarations to the top of
their block (leaving the initialiser as an assignment), but deliberately skips
multi-declarator ones (`TYPE a, b;`). When such a declaration was surrounded by
declarations that got hoisted, it can end up *after* an assignment statement,
which is illegal in C89. This flags exactly those, so they can be hoisted by
hand (move the declaration up into the block's declaration区, leave assignments).

It does NOT modify files - it only reports `path:line: text  [prev: ...]`.
Usage:  python scripts/find_split_multidecl.py <file> [...]
"""
import re
import sys

# A block-level variable declaration line that c89ize.py may have left in place:
# a type, then declarator(s), ending in ';' (optionally followed by a comment).
# Covers both multi-declarator (`TYPE a, b;`) and single declarations c89ize
# skipped because a trailing comment hid the ';' from its stricter regex.
MULT = re.compile(
    r'^[ \t]+(?:const[ \t]+)?(?:unsigned[ \t]+|signed[ \t]+)?'
    r'(?:int|char|short|long|float|double|bool|void|css_fixed|css_unit|'
    r'css_color|size_t|nserror|colour|lwc_string|dom_string|plot_font_style_t|'
    r'enum[ \t]+\w+|struct[ \t]+\w+[ \t]*\*+|\w+_t)[ \t]+[^;{}()]*;'
    r'[ \t]*(?:/\*.*)?$')

# A declaration whose initialiser spills onto the next line: `TYPE name = ...`
# with no ';' on this line. c89ize.py only matches single-line decls, so these
# are skipped too and break C89 if they follow a statement.
ML = re.compile(
    r'^[ \t]+(?:const[ \t]+)?(?:unsigned[ \t]+|signed[ \t]+)?'
    r'(?:int|char|short|long|float|double|bool|void|css_fixed|css_unit|'
    r'css_color|size_t|nserror|colour|lwc_string|dom_string|plot_font_style_t|'
    r'enum[ \t]+\w+|struct[ \t]+\w+[ \t]*\*+|\w+_t)[ \t]+\*?\w+[ \t]*=[^;]*$')

# An assignment / member-store statement (what c89ize leaves behind).
ASSIGN = re.compile(r'^[A-Za-z_]\w*[ \t]*(?:\[[^\]]*\]|\.\w+|->\w+)*[ \t]*[-+*/|&]?=[^=]')

def main():
    total = 0
    for path in sys.argv[1:]:
        lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
        for i in range(1, len(lines)):
            if not (MULT.match(lines[i]) or ML.match(lines[i])):
                continue
            j = i - 1
            while j >= 0 and lines[j].strip() == '':
                j -= 1
            prev = lines[j].strip()
            # Broken iff the preceding statement is an assignment or block end -
            # but NOT a block *start* ("...) {"), where a declaration is legal.
            if (ASSIGN.match(prev) or prev.endswith('}')) \
                    and not prev.endswith('{'):
                print("%s:%d: %s   [prev: %s]" %
                      (path, i + 1, lines[i].strip(), prev[:50]))
                total += 1
    print("--- %d split multi-declarator(s) ---" % total)

if __name__ == '__main__':
    main()
