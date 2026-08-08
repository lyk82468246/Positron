# tiny-regex-c

Positron vendors `re.c` and `re.h` from:

- Project: <https://github.com/kokke/tiny-regex-c>
- Commit: `f2632c6d9ed25272987471cdb8b70395c2460bdb`
- Retrieved: 2026-08-07
- License: public domain / Unlicense; see `LICENSE`.

The source is compiled as C89 by the ARMV4I `positron_core` project.
`positron_core/pcore_pattern.c` provides the adapter and rejects unsupported
syntax before calling it.

Positron carries one local correctness patch in `re.c`: when matching a
literal `-` at the start or end of a character class that also contains range
operators, the upstream loop used to return failure at the first range `-`
instead of continuing to the literal `-`. TEST108 covers the fixed
`[A-Z0-9-]+` behavior.

The adapter currently accepts ASCII literals, `.`, `^`, `$`, `*`, `+`, `?`,
literal/range character classes, and the engine's `\\d`, `\\D`, `\\s`,
`\\S`, `\\w`, and `\\W` escapes. It intentionally rejects groups,
alternation, brace quantifiers, Unicode escapes/properties, inverted classes,
and escaped classes inside `[ ... ]`. Invalid or unsupported patterns are
ignored by constraint validation, as required for conservative page behavior.
