#!/usr/bin/env python3
"""Apply the narrow VS2008 C++ port layer to Expat 2.8.2 xmlparse.c.

Expat 2.8.2 requires C99, while the WM6 C compiler is C89-only. Compiling the
implementation as C++ preserves Expat's public extern-C ABI and avoids moving
security-sensitive local declarations. The remaining incompatibility is C's
implicit void-pointer conversion. This script inserts one internal conversion
proxy and five exact casts for allocator calls outside Expat's MALLOC macro.

The exact upstream SHA and anchors make this fail closed on a version change.
"""

from hashlib import sha256
from pathlib import Path
import sys


UPSTREAM_SHA256 = "73851a9f793c5fc94c4d116c7aa4a3eaf7847e8d3a3b6ff64a2b93dbf5d6b600"
MARKER = "class PositronExpatAllocation"


def replace_once(text, old, new):
    count = text.count(old)
    if count != 1:
        raise SystemExit("expected one Expat 2.8.2 anchor, found %d:\n%s" %
                         (count, old))
    return text.replace(old, new, 1)


def replace_or_confirm(text, old, new):
    if new in text:
        return text, False
    return replace_once(text, old, new), True


def main():
    if len(sys.argv) != 2:
        print("usage: port_expat_vs2008.py <xmlparse.c>")
        return 2

    path = Path(sys.argv[1])
    raw = path.read_bytes()
    text = raw.decode("utf-8").replace("\r\n", "\n")

    changed = False
    if MARKER not in text:
        digest = sha256(raw).hexdigest()
        if digest != UPSTREAM_SHA256:
            raise SystemExit("unsupported xmlparse.c SHA-256: %s" % digest)

        macro_anchor = """#if XML_GE == 1
#  define MALLOC(parser, s) (expat_malloc((parser), (s), __LINE__))
"""
        proxy = """#ifdef __cplusplus
class PositronExpatAllocation {
public:
  explicit PositronExpatAllocation(void *pointer) : pointer_(pointer) {}

  template <typename T> operator T *() const {
    return static_cast<T *>(pointer_);
  }

private:
  void *pointer_;
};
#  define POSITRON_EXPAT_ALLOC(pointer) PositronExpatAllocation(pointer)
#else
#  define POSITRON_EXPAT_ALLOC(pointer) (pointer)
#endif

#if XML_GE == 1
#  define MALLOC(parser, s) \
    POSITRON_EXPAT_ALLOC(expat_malloc((parser), (s), __LINE__))
"""
        text = replace_once(text, macro_anchor, proxy)
        text = replace_once(
            text,
            "#  define REALLOC(parser, p, s) (expat_realloc((parser), (p), (s), __LINE__))",
            "#  define REALLOC(parser, p, s) \\\n"
            "    POSITRON_EXPAT_ALLOC(expat_realloc((parser), (p), (s), __LINE__))")
        text = replace_once(
            text,
            "#  define MALLOC(parser, s) (parser->m_mem.malloc_fcn((s)))",
            "#  define MALLOC(parser, s) \\\n"
            "    POSITRON_EXPAT_ALLOC(parser->m_mem.malloc_fcn((s)))")
        text = replace_once(
            text,
            "#  define REALLOC(parser, p, s) (parser->m_mem.realloc_fcn((p), (s)))",
            "#  define REALLOC(parser, p, s) \\\n"
            "    POSITRON_EXPAT_ALLOC(parser->m_mem.realloc_fcn((p), (s)))")
        text = replace_once(
            text,
            "parser = memsuite->malloc_fcn(sizeof(struct XML_ParserStruct));",
            "parser = (XML_Parser)memsuite->malloc_fcn(sizeof(struct XML_ParserStruct));")
        text = replace_once(
            text,
            "parser = malloc(sizeof(struct XML_ParserStruct));",
            "parser = (XML_Parser)malloc(sizeof(struct XML_ParserStruct));")
        text = replace_once(text, "(void)sip24_valid;", "(void)&sip24_valid;")
        changed = True

    direct_allocators = [
        ("char *const newBuf = parser->m_mem.malloc_fcn(bufferSize);",
         "char *const newBuf = (char *)parser->m_mem.malloc_fcn(bufferSize);"),
        ("XML_Content *content = parser->m_mem.malloc_fcn(sizeof(XML_Content));",
         "XML_Content *content = (XML_Content *)parser->m_mem.malloc_fcn(sizeof(XML_Content));"),
        ("ret = parser->m_mem.malloc_fcn(allocsize);",
         "ret = (XML_Content *)parser->m_mem.malloc_fcn(allocsize);"),
    ]
    for old, new in direct_allocators:
        text, did_change = replace_or_confirm(text, old, new)
        changed = changed or did_change

    if changed:
        path.write_text(text, encoding="utf-8", newline="\n")
        print("port_expat_vs2008: applied Expat 2.8.2 C++ compatibility layer")
    else:
        print("port_expat_vs2008: already applied")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
