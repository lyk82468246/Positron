#!/usr/bin/env python3
"""Generate the VS2008/C89 libcss counter-style implementation.

The authoritative NetSurf/libcss source uses C99 designated initializers and
UTF-8 source literals.  VS2008 accepts neither reliably.  This converter knows
the two upstream aggregate layouts, emits positional C89 initializers, escapes
non-ASCII string bytes as fixed-width octal, then applies the shared C89
declaration rules.  Unknown fields or syntax fail loudly.
"""

import re
from pathlib import Path

import c89ize


ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "netsurf-all-3.11" / "libcss" / "src" / "select" /
          "format_list_style.c")
DESTINATION = (ROOT / "netsurf-all-3.11" / "libcss" / "src" / "select" /
               "positron_format_list_style.c")


def escape_utf8_literals(text):
    out = []
    in_string = False
    escaped = False
    for char in text:
        if in_string:
            if escaped:
                out.append(char)
                escaped = False
            elif char == "\\":
                out.append(char)
                escaped = True
            elif char == '"':
                out.append(char)
                in_string = False
            elif ord(char) > 127:
                out.extend("\\%03o" % byte for byte in char.encode("utf-8"))
            else:
                out.append(char)
        else:
            out.append(char)
            if char == '"':
                in_string = True
    result = "".join(out)
    if any(ord(char) > 127 for char in result):
        raise RuntimeError("non-ASCII text remains outside a C string literal")
    return result


def split_top_level(text):
    parts = []
    start = 0
    depths = {"{": 0, "(": 0, "[": 0}
    matching = {"}": "{", ")": "(", "]": "["}
    in_string = False
    escaped = False
    for index, char in enumerate(text):
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char in depths:
            depths[char] += 1
        elif char in matching:
            depths[matching[char]] -= 1
        elif char == "," and all(depth == 0 for depth in depths.values()):
            parts.append(text[start:index].strip())
            start = index + 1
    tail = text[start:].strip()
    if tail:
        parts.append(tail)
    if in_string or any(depth != 0 for depth in depths.values()):
        raise RuntimeError("unbalanced aggregate initializer")
    return parts


def designated_values(text, allowed):
    values = {}
    for part in split_top_level(text):
        match = re.match(r"^\.([A-Za-z_]\w*)\s*=\s*(.*)$", part, re.S)
        if match is None:
            raise RuntimeError("unsupported initializer entry: %s" % part[:80])
        name = match.group(1)
        if name not in allowed or name in values:
            raise RuntimeError("unknown or duplicate initializer field: " + name)
        values[name] = match.group(2).strip()
    return values


def nested_values(value, fields, defaults):
    value = value.strip()
    if not (value.startswith("{") and value.endswith("}")):
        raise RuntimeError("nested initializer is not braced: " + value)
    found = designated_values(value[1:-1], fields)
    return "{ %s }" % ", ".join(found.get(name, defaults[index])
                                     for index, name in enumerate(fields))


def find_matching_brace(text, opening):
    depth = 0
    in_string = False
    escaped = False
    for index in range(opening, len(text)):
        char = text[index]
        if in_string:
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                in_string = False
            continue
        if char == '"':
            in_string = True
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    raise RuntimeError("unterminated aggregate initializer")


COUNTER_FIELDS = (
    "name", "system", "fallback", "symbols", "weights", "items",
    "range", "pad", "negative", "prefix", "suffix"
)
COUNTER_DEFAULTS = (
    "NULL", "NULL", "NULL", "NULL", "NULL", "0",
    "{ 0, 0 }", "{ 0, { 0 } }", "{ NULL, NULL }", "NULL", "NULL"
)


def counter_initializer(body):
    found = designated_values(body, COUNTER_FIELDS)
    found["range"] = nested_values(found["range"], ("start", "end"),
                                     ("0", "0")) if "range" in found else \
            COUNTER_DEFAULTS[6]
    found["pad"] = nested_values(found["pad"], ("length", "value"),
                                   ("0", "{ 0 }")) if "pad" in found else \
            COUNTER_DEFAULTS[7]
    found["negative"] = nested_values(
        found["negative"], ("pre", "post"), ("NULL", "NULL")) \
            if "negative" in found else COUNTER_DEFAULTS[8]
    lines = []
    for index, name in enumerate(COUNTER_FIELDS):
        lines.append("\t%s" % found.get(name, COUNTER_DEFAULTS[index]))
    return "{\n%s\n}" % ",\n".join(lines)


def rewrite_counter_styles(text):
    pattern = re.compile(
        r"static\s+(const\s+)?struct list_counter_style\s+"
        r"([A-Za-z_]\w*)\s*=\s*\{")
    pieces = []
    position = 0
    count = 0
    while True:
        match = pattern.search(text, position)
        if match is None:
            pieces.append(text[position:])
            break
        opening = match.end() - 1
        closing = find_matching_brace(text, opening)
        semicolon = closing + 1
        while semicolon < len(text) and text[semicolon].isspace():
            semicolon += 1
        if semicolon >= len(text) or text[semicolon] != ";":
            raise RuntimeError("counter initializer lacks a semicolon")
        pieces.append(text[position:match.start()])
        qualifier = "const " if match.group(1) is not None else ""
        pieces.append("static %sstruct list_counter_style %s = %s;" %
                      (qualifier, match.group(2),
                       counter_initializer(text[opening + 1:closing])))
        position = semicolon + 1
        count += 1
    if count < 10:
        raise RuntimeError("unexpectedly few counter styles: %d" % count)
    return "".join(pieces), count


def rewrite_numeric_initializer(text):
    pattern = re.compile(r"struct numeric nval\s*=\s*\{(.*?)\};", re.S)
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        raise RuntimeError("expected one local numeric initializer")
    match = matches[0]
    fields = ("val", "len", "used", "negative")
    values = designated_values(match.group(1), fields)
    replacement = "struct numeric nval = { %s };" % ", ".join(
        values.get(name, "0") for name in fields)
    return text[:match.start()] + replacement + text[match.end():]


def main():
    source = SOURCE.read_text(encoding="utf-8")
    source = escape_utf8_literals(source)
    generated, counter_count = rewrite_counter_styles(source)
    generated = rewrite_numeric_initializer(generated)
    generated, c89_changes = c89ize.transform(generated)
    if re.search(r"^\s*\.[A-Za-z_]", generated, re.M):
        raise RuntimeError("designated initializer remains in generated C")
    banner = ("/* Generated by scripts/port_list_style_vs2008.py from\n"
              " * libcss/src/select/format_list_style.c. Do not edit. */\n")
    DESTINATION.write_text(banner + generated + "\n", encoding="ascii",
                           newline="\n")
    print("generated %s: %d counter styles, %d C89 declaration changes" %
          (DESTINATION, counter_count, c89_changes))


if __name__ == "__main__":
    main()
