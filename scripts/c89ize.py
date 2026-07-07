#!/usr/bin/env python3
"""
c89ize.py - rewrite NetSurf libcss C99 constructs so the VS2008 (C89) C
compiler accepts them. Three conservative transforms:

1. Simple NetSurf plot style designated initializers -> positional C89
   initializers for known field order (`plot_style_t`, `plot_font_style_t`).

2. Mid-block variable declarations -> hoist the declarator to the top of its
   enclosing block, leaving the initializer in place as an assignment:
       stmt; TYPE name = init;  ->  { TYPE name;  ...stmt;  name = init; ... }

3. for-loop declarations -> hoist the loop variable to the enclosing block top:
       for (TYPE i = init; ...)  ->  TYPE i;  ...  for (i = init; ...)

A proper brace stack is maintained by tracking, per line, the MINIMUM depth
reached (so "} else if (...) {" correctly closes the previous block and opens a
fresh sibling - its body decls are NOT mis-seen as mid-block) and the END depth
(so newly opened blocks are pushed fresh with seen=False).

SAFETY: multi-declarator declarations ("TYPE a = .., b ..;") and multi-variable
for-inits are left untouched (a top-level comma is the signal) - splitting them
is error-prone, so they are left for manual fixup (rare; the compiler flags
them). Declarations already preceding every statement in their block are left
alone (legal C89). Idempotent. Audit `git diff`, then compile.

Usage:  python scripts/c89ize.py <file> [...]
"""
import re
import sys

CTRL = {'return', 'if', 'else', 'for', 'while', 'switch', 'case', 'default',
        'do', 'goto', 'break', 'continue', 'sizeof', 'typedef'}

AGGREGATE_START = re.compile(
    r'^\s*(?:typedef\s+)?(?:struct|union|enum)(?:\s+[A-Za-z_]\w*)?\s*\{')

DECL = re.compile(
    r'^(?P<ind>[ \t]+)'
    r'(?P<head>(?:(?:const|static|volatile|register|unsigned|signed|struct|enum|union)\s+)*'
    r'[A-Za-z_]\w*)'
    r'(?P<sep>\s*\*+\s*|\s+)'
    r'(?P<name>[A-Za-z_]\w*)'
    r'(?P<arr>\s*\[[^\]]*\])?'
    r'\s*(?P<init>=[^;]*)?;[ \t]*$')

DECL_INIT_START = re.compile(
    r'^\s*'
    r'(?:(?:const|static|volatile|register|unsigned|signed|struct|enum|union)\s+)*'
    r'[A-Za-z_]\w*'
    r'(?:\s*\*+\s*|\s+)'
    r'[A-Za-z_]\w*'
    r'(?:\s*\[[^\]]*\])?'
    r'\s*=')

DECL_LIKE = re.compile(
    r'^\s*'
    r'(?P<head>(?:(?:const|static|volatile|register|unsigned|signed|struct|enum|union)\s+)*'
    r'[A-Za-z_]\w*)'
    r'(?:\s*\*+\s*|\s+)'
    r'[A-Za-z_]\w*')

FORD = re.compile(
    r'^(?P<pre>\s*for\s*\(\s*)'
    r'(?P<type>(?:const\s+)?(?:unsigned\s+|signed\s+|struct\s+)?[A-Za-z_]\w*(?:\s*\*+\s*|\s+))'
    r'(?P<name>[A-Za-z_]\w*)'
    r'(?P<post>\s*=.*)$')

DESIGNATED_TYPES = {
    'plot_style_t': [
        'stroke_type', 'stroke_width', 'stroke_colour',
        'fill_type', 'fill_colour',
    ],
    'plot_font_style_t': [
        'families', 'family', 'size', 'weight',
        'flags', 'background', 'foreground',
    ],
}

DESIGNATED_START = re.compile(
    r'^(?P<ind>\s*)'
    r'(?P<decl>(?:(?:static|const)\s+)*'
    r'(?P<type>plot_style_t|plot_font_style_t)\s+'
    r'(?:const\s+)?[A-Za-z_]\w*)\s*=\s*\{\s*$')

DESIGNATED_ENTRY = re.compile(
    r'^\s*\.(?P<field>[A-Za-z_]\w*)\s*=\s*(?P<value>.*?)(?:,)?\s*$')


def transform_designated_structs(text):
    """Rewrite simple multiline designated initializers for known NetSurf
    structs. This deliberately avoids nested values and unknown types."""
    lines = text.split('\n')
    out = []
    i = 0
    n = 0

    while i < len(lines):
        m = DESIGNATED_START.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue

        fields = DESIGNATED_TYPES[m.group('type')]
        values = {}
        j = i + 1
        ok = True

        while j < len(lines):
            s = lines[j].strip()
            if s == '};':
                break
            em = DESIGNATED_ENTRY.match(lines[j])
            if not em:
                ok = False
                break
            if em.group('field') not in fields:
                ok = False
                break
            values[em.group('field')] = em.group('value')
            j += 1

        if ok and j < len(lines) and lines[j].strip() == '};':
            out.append(lines[i])
            out.append('%s\t%s' % (
                m.group('ind'),
                ', '.join(values.get(field, '0') for field in fields)))
            out.append(lines[j])
            i = j + 1
            n += 1
        else:
            out.append(lines[i])
            i += 1

    return '\n'.join(out), n


def has_top_level_comma(s):
    depth = 0
    st = 'code'
    i, L = 0, len(s)
    while i < L:
        c = s[i]
        if st == 'code':
            if c == '"':
                st = 'str'
            elif c == "'":
                st = 'chr'
            elif c in '([{':
                depth += 1
            elif c in ')]}':
                depth -= 1
            elif c == ',' and depth == 0:
                return True
        elif st == 'str':
            if c == '\\':
                i += 2; continue
            if c == '"':
                st = 'code'
        elif st == 'chr':
            if c == '\\':
                i += 2; continue
            if c == "'":
                st = 'code'
        i += 1
    return False


def line_depths(text):
    """Return (starts, mins): start-of-line brace depth, and the minimum depth
    reached within each line (counting only braces in real code)."""
    starts, mins = [], []
    d = 0
    state = 'code'
    for line in text.split('\n'):
        starts.append(d)
        lo = d
        j, L = 0, len(line)
        while j < L:
            c = line[j]
            two = line[j:j + 2]
            if state == 'code':
                if two == '//':
                    break
                if two == '/*':
                    state = 'block'; j += 2; continue
                if c == '"':
                    state = 'str'
                elif c == "'":
                    state = 'chr'
                elif c == '{':
                    d += 1
                elif c == '}':
                    d -= 1
                    if d < lo:
                        lo = d
            elif state == 'block':
                if two == '*/':
                    state = 'code'; j += 2; continue
            elif state == 'str':
                if c == '\\':
                    j += 2; continue
                if c == '"':
                    state = 'code'
            elif state == 'chr':
                if c == '\\':
                    j += 2; continue
                if c == "'":
                    state = 'code'
            j += 1
        mins.append(lo)
    return starts, mins


def is_stmt(stripped):
    m = None

    if not stripped:
        return False
    if stripped.startswith(('//', '/*', '*', '#')):
        return False
    if stripped in ('{', '}'):
        return False
    if re.match(r'^[A-Za-z_]\w*:$', stripped):
        return False
    if DECL_INIT_START.match(stripped):
        return False
    m = DECL_LIKE.match(stripped)
    if m and m.group('head').split()[0] not in CTRL and stripped.endswith(';'):
        return False
    return True


def transform(text):
    text, ndesignated = transform_designated_structs(text)
    lines = text.split('\n')
    starts, mins = line_depths(text)
    n = len(lines)
    inserts = {}
    replaces = {}
    nchg = ndesignated
    stack = []
    aggregate_depth = None
    decl_cont = False

    for i in range(n):
        stripped = lines[i].strip()

        if decl_cont:
            if stripped.endswith(';'):
                decl_cont = False
            continue
        if DECL_INIT_START.match(stripped) and not stripped.endswith(';'):
            decl_cont = True
            continue

        if aggregate_depth is not None:
            next_depth = starts[i + 1] if i + 1 < n else 0
            if next_depth <= aggregate_depth:
                aggregate_depth = None
            else:
                continue

        if AGGREGATE_START.match(lines[i]):
            aggregate_depth = starts[i]
            continue

        # Close blocks that ended within this line (e.g. leading '}' of
        # "} else if (...) {"). Use the minimum depth reached on the line.
        while len(stack) > mins[i]:
            stack.pop()

        cur = stack[-1] if (len(stack) >= 1 and mins[i] >= 1) else None
        if cur is not None:
            ln = lines[i]

            mf = FORD.match(ln)
            if mf and mf.group('type').split()[0] not in CTRL and \
                    not has_top_level_comma(mf.group('post').split(';', 1)[0]):
                typ = mf.group('type'); name = mf.group('name')
                lead = re.match(r'^\s*', ln).group(0)
                if name not in cur['names']:
                    decl = "%s%s%s;" % (lead, typ.rstrip(),
                                        (' ' + name) if not typ.rstrip().endswith('*') else name)
                    inserts.setdefault(cur['insert'], []).append(decl)
                    cur['names'].add(name)
                replaces[i] = "%s%s%s" % (mf.group('pre'), name, mf.group('post'))
                cur['seen'] = True
                nchg += 1
            else:
                m = DECL.match(ln)
                if m and m.group('head').split()[0] not in CTRL:
                    init = m.group('init')
                    if init and has_top_level_comma(init):
                        pass  # multi-declarator: leave for manual fixup
                    elif cur['seen']:
                        ind = m.group('ind'); head = m.group('head'); sep = m.group('sep')
                        name = m.group('name'); arr = m.group('arr') or ''
                        if name not in cur['names']:
                            inserts.setdefault(cur['insert'], []).append(
                                "%s%s%s%s%s;" % (ind, head, sep, name, arr))
                            cur['names'].add(name)
                        replaces[i] = ("%s%s%s %s;" % (ind, name, arr, init)) if init else None
                        nchg += 1
                elif is_stmt(ln.strip()):
                    cur['seen'] = True

        # Open blocks started within this line; each is fresh (seen=False).
        de = starts[i + 1] if i + 1 < n else 0
        while len(stack) < de:
            stack.append({'insert': i, 'seen': False, 'names': set()})

    if nchg == 0:
        return text, 0

    out = []
    for idx, ln in enumerate(lines):
        if idx in replaces:
            if replaces[idx] is not None:
                out.append(replaces[idx])
        else:
            out.append(ln)
        if idx in inserts:
            out.extend(inserts[idx])
    return '\n'.join(out), nchg


def main():
    total = 0
    for path in sys.argv[1:]:
        with open(path, 'r', newline='') as f:
            raw = f.read()
        crlf = '\r\n' in raw
        text = raw.replace('\r\n', '\n')
        new, n = transform(text)
        if n:
            if crlf:
                new = new.replace('\n', '\r\n')
            with open(path, 'w', newline='') as f:
                f.write(new)
        print("%s: %d change(s)" % (path, n))
        total += n
    print("total: %d" % total)


if __name__ == '__main__':
    main()
