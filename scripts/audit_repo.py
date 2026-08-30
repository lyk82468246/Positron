#!/usr/bin/env python3
"""Audit source completeness and third-party metadata for a fresh clone."""

from __future__ import print_function

import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
try:
    from urllib.parse import unquote
except ImportError:
    from urllib import unquote


ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))

REQUIRED = (
    "LICENSE",
    "THIRD_PARTY.md",
    "Positron.sln",
    "positron_tls/mbedtls/LICENSE",
    "positron_tls/mbedtls/apache-2.0.txt",
    "positron_tls/mbedtls/include/mbedtls/version.h",
    "positron_json/cjson/LICENSE",
    "positron_expat/COPYING",
    "netsurf-all-3.11/netsurf/COPYING",
    "third_party/libjpeg-turbo/LICENSE.md",
    "third_party/noto-symbols/OFL.txt",
    "third_party/noto-symbols2/OFL.txt",
    "third_party/noto-emoji/OFL.txt",
)

EXPECTED_VERSIONS = (
    ("positron_tls/mbedtls/include/mbedtls/version.h",
     ("MBEDTLS_VERSION_MAJOR", "2"),
     ("MBEDTLS_VERSION_MINOR", "16"),
     ("MBEDTLS_VERSION_PATCH", "12")),
    ("positron_json/cjson/cJSON.h",
     ("CJSON_VERSION_MAJOR", "1"),
     ("CJSON_VERSION_MINOR", "7"),
     ("CJSON_VERSION_PATCH", "18")),
)

DOC_EXCLUDES = (
    "netsurf-all-3.11/",
    "positron_tls/mbedtls/",
    "third_party/libjpeg-turbo/",
)
DOC_INCLUDE_EXCEPTIONS = (
    "positron_tls/mbedtls/UPSTREAM.md",
    "third_party/libjpeg-turbo/POSITRON_PORT.md",
)

# These documents have deliberately narrow roles.  The generous limits count
# non-whitespace characters, so changing Markdown wrapping cannot make a
# document pass or fail.  They are gross-growth tripwires, not writing targets.
DOC_ROLE_TEXT_LIMITS = {
    "README.md": 24000,
    "docs/ARCHITECTURE.md": 60000,
    "docs/TESTING.md": 60000,
    ".agents/HANDOFF.md": 24000,
    ".agents/KNOWN_LIMITATIONS.md": 40000,
    ".agents/ROADMAP.md": 30000,
    "positron_browser/README.md": 30000,
    "positron_core/README.md": 30000,
    "test_host/README.md": 30000,
}

# A paragraph or individual list item this large is almost certainly carrying
# several batches or topics.  It remains deliberately high so ordinary long
# technical explanations are not forced into artificial fragments.
DOC_PROSE_BLOCK_LIMIT = 3000

DOC_STRUCTURE_PATHS = set(DOC_ROLE_TEXT_LIMITS)
DOC_STRUCTURE_PATHS.update((
    "AGENTS.md",
    "docs/README.md",
    "docs/BUILDING.md",
    "docs/TROUBLESHOOTING.md",
    "docs/NIGHTLY_RELEASE.md",
    ".agents/README.md",
    ".agents/DEBUGGING.md",
))

# `test_host` may consume public DLLs, but it must never compile a product
# implementation into the harness or define a public product entry point.
# This is a deliberately narrow static guard: it catches the easy ways to
# move ownership back into the host while the architecture review remains the
# authority for less mechanical business-semantics decisions.  The line
# prefix must look like a C return type; calls such as `if (PBrowser_...)` and
# `return PBrowser_...` must not be reported.
PRODUCT_IMPLEMENTATION_RE = re.compile(
    r"(?m)^[ \t]*(?!(?:if|for|while|switch|return|sizeof)\b)"
    r"(?:static\s+)?(?:const\s+|volatile\s+|unsigned\s+|signed\s+|"
    r"long\s+|short\s+|struct\s+|enum\s+|[A-Za-z_]\w*\s+|\*+)+"
    r"P(?:Tls|Json|Http|Image|Script|Core|Browser)_[A-Za-z0-9_]+\s*\(")
PRODUCT_SOURCE_INCLUDE_RE = re.compile(
    r"(?m)^\s*#\s*include\s*[\"<][^\" >]*positron_"
    r"(?:tls|json|http|image|script|core|browser)[^\" >]*\.c[\">]")


def audit_test_host_boundary(errors):
    """Reject product implementation/source inclusion in the test host."""
    host_root = os.path.join(ROOT, "test_host")
    if not os.path.isdir(host_root):
        return 0

    checked = 0
    for base, dirs, files in os.walk(host_root):
        parts = set(relpath(base).split("/"))
        if "bin" in parts or "obj" in parts:
            dirs[:] = []
            continue
        for filename in files:
            if not filename.lower().endswith((".c", ".h", ".cpp", ".cxx")):
                continue
            path = os.path.join(base, filename)
            name = relpath(path)
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    text = handle.read()
            except (IOError, UnicodeError) as exc:
                errors.append("cannot read host source %s: %s" % (name, exc))
                continue
            checked += 1
            if PRODUCT_SOURCE_INCLUDE_RE.search(text) is not None:
                errors.append(
                    "%s includes a product implementation source; "
                    "test_host may consume DLL headers only" % name)
            for match in PRODUCT_IMPLEMENTATION_RE.finditer(text):
                line = text.count("\n", 0, match.start()) + 1
                errors.append(
                    "%s:%d defines a public product API; move the "
                    "implementation into its owning DLL" % (name, line))
    return checked


def relpath(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def normalized_text_size(text):
    """Measure content scale independently from wrapping and blank lines."""
    return len(re.sub(r"\s+", "", text))


def load_tracked():
    try:
        data = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "-z"],
            stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError):
        return None
    if not isinstance(data, str):
        data = data.decode("utf-8", "replace")
    return set(item.replace("\\", "/") for item in data.split("\0") if item)


def load_worktree_files():
    """Return visible tracked/untracked files, excluding deletions and ignores."""
    try:
        data = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "--cached", "--others",
             "--exclude-standard", "-z"],
            stderr=subprocess.STDOUT)
        deleted_data = subprocess.check_output(
            ["git", "-C", ROOT, "ls-files", "--deleted", "-z"],
            stderr=subprocess.STDOUT)
    except (OSError, subprocess.CalledProcessError):
        return None
    if not isinstance(data, str):
        data = data.decode("utf-8", "replace")
    if not isinstance(deleted_data, str):
        deleted_data = deleted_data.decode("utf-8", "replace")
    visible = set(item.replace("\\", "/")
                  for item in data.split("\0") if item)
    deleted = set(item.replace("\\", "/")
                  for item in deleted_data.split("\0") if item)
    return visible - deleted


def iter_projects():
    for base, dirs, files in os.walk(ROOT):
        parts = set(relpath(base).split("/"))
        if ".git" in parts or "bin" in parts or "obj" in parts:
            dirs[:] = []
            continue
        for name in files:
            if name.lower().endswith(".vcproj"):
                yield os.path.join(base, name)


def xml_local_name(tag):
    return tag.rsplit("}", 1)[-1]


def audit_project(project, tracked, errors):
    try:
        with open(project, "rb") as handle:
            raw = handle.read()
        declaration = re.match(br"\s*<\?xml[^>]*encoding=[\"']([^\"']+)", raw)
        encoding = declaration.group(1).decode("ascii") if declaration else "utf-8"
        text = raw.decode(encoding)
        text = re.sub(r"^\s*<\?xml[^>]*\?>", "", text, count=1)
        root = ET.fromstring(text)
    except (IOError, LookupError, UnicodeError, ValueError, ET.ParseError) as exc:
        errors.append("cannot parse %s: %s" % (relpath(project), exc))
        return 0

    checked = 0
    for node in root.iter():
        if xml_local_name(node.tag) == "File":
            value = node.attrib.get("RelativePath")
            if not value:
                continue
            full = os.path.abspath(os.path.join(os.path.dirname(project),
                                                value.replace("\\", os.sep)))
            source_rel = relpath(full)
            checked += 1
            if not os.path.isfile(full):
                errors.append("%s references missing %s" %
                              (relpath(project), source_rel))
            elif tracked is not None and source_rel not in tracked:
                errors.append("%s references untracked %s" %
                              (relpath(project), source_rel))
            if (relpath(project).lower() == "test_host/test_host.vcproj" and
                    re.match(r"^positron_[^/]+/.*\.c$", source_rel,
                             re.IGNORECASE)):
                errors.append(
                    "%s compiles product source %s; test_host must consume "
                    "the owning DLL instead" %
                    (relpath(project), source_rel))

        for attr in ("AdditionalIncludeDirectories",
                     "AdditionalLibraryDirectories"):
            value = node.attrib.get(attr, "")
            for entry in value.split(";"):
                entry = entry.strip().strip('"')
                if re.match(r"^[A-Za-z]:[\\/]", entry):
                    errors.append("%s has absolute %s path: %s" %
                                  (relpath(project), attr, entry))
    return checked


def audit_versions(errors):
    for spec in EXPECTED_VERSIONS:
        path = os.path.join(ROOT, spec[0].replace("/", os.sep))
        try:
            with open(path, "r") as handle:
                text = handle.read()
        except IOError as exc:
            errors.append("cannot read version file %s: %s" % (spec[0], exc))
            continue
        for macro, expected in spec[1:]:
            match = re.search(r"^\s*#\s*define\s+%s\s+(\S+)" %
                              re.escape(macro), text, re.MULTILINE)
            if match is None or match.group(1) != expected:
                actual = match.group(1) if match else "missing"
                errors.append("%s expected %s=%s, found %s" %
                              (spec[0], macro, expected, actual))


def audit_markdown_links(documents, errors):
    if documents is None:
        return 0, 0
    document_count = 0
    link_count = 0
    link_re = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
    for name in sorted(documents):
        if not name.lower().endswith(".md"):
            continue
        if (name.startswith(DOC_EXCLUDES) and
                name not in DOC_INCLUDE_EXCEPTIONS):
            continue
        document_count += 1
        path = os.path.join(ROOT, name.replace("/", os.sep))
        try:
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
        except (IOError, UnicodeError) as exc:
            errors.append("cannot read documentation %s: %s" % (name, exc))
            continue
        for match in link_re.finditer(text):
            target = match.group(1).strip()
            if target.startswith("<") and target.endswith(">"):
                target = target[1:-1]
            if (not target or target.startswith("#") or
                    re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:", target)):
                continue
            target = unquote(target.split("#", 1)[0])
            link_count += 1
            full = os.path.normpath(os.path.join(os.path.dirname(path), target))
            if not os.path.exists(full):
                errors.append("%s has broken local link: %s" % (name, target))
    return document_count, link_count


def is_stable_reader_document(name):
    if name in (
            "README.md", "docs/README.md", "docs/ARCHITECTURE.md",
            "docs/BUILDING.md", "docs/TESTING.md",
            "docs/TROUBLESHOOTING.md", "docs/NIGHTLY_RELEASE.md"):
        return True
    return re.match(
        r"^(?:positron_[^/]+|test_host|assets/fonts|samples(?:/[^/]+)?)"
        r"/README\.md$", name) is not None


def iter_markdown_prose_blocks(lines):
    """Yield semantic prose blocks without depending on physical wrapping."""
    blocks = []
    current = []
    chunk_start = 0
    in_fence = False

    def emit_chunk(chunk, start):
        if not chunk:
            return []
        stripped = [line.strip() for line in chunk]
        if (stripped[0].startswith("#") or
                stripped[0].startswith(">") or
                stripped[0].startswith("<") or
                stripped[0].startswith("|") or
                chunk[0].startswith("    ") or
                re.match(r"^\[[^]]+\]:", stripped[0]) or
                re.match(r"^(?:---+|\*\*\*+|___+)$", stripped[0])):
            return []

        marker_re = re.compile(r"^\s*(?:[-+*]|[0-9]+[.)])\s+")
        if marker_re.match(chunk[0]):
            items = []
            item = []
            item_start = start
            for offset, line in enumerate(chunk):
                if marker_re.match(line):
                    if item:
                        items.append((item_start, " ".join(item)))
                    item_start = start + offset
                    item = [marker_re.sub("", line, count=1).strip()]
                elif item:
                    item.append(line.strip())
            if item:
                items.append((item_start, " ".join(item)))
            return items

        return [(start, " ".join(stripped))]

    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if stripped.startswith("```") or stripped.startswith("~~~"):
            if not in_fence:
                blocks.extend(emit_chunk(current, chunk_start))
                current = []
                chunk_start = 0
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if not stripped:
            blocks.extend(emit_chunk(current, chunk_start))
            current = []
            chunk_start = 0
            continue
        if not current:
            chunk_start = number
        current.append(line)

    blocks.extend(emit_chunk(current, chunk_start))
    return blocks


def audit_document_structure(documents, errors):
    """Enforce document roles without treating prose wording as an API."""
    if documents is None:
        return 0
    checked = 0
    batch_re = re.compile(r"\bnext[0-9]+\b", re.IGNORECASE)
    run_re = re.compile(r"tmp/device-runs/[0-9]", re.IGNORECASE)
    roadmap_heading_re = re.compile(
        r"^#{1,6}\s+.*(?:next[0-9]+|已完成)", re.IGNORECASE)
    heading_re = re.compile(r"^#{1,2}\s+(.+?)\s*$")

    for name in sorted(documents):
        if not name.lower().endswith(".md"):
            continue
        if (name.startswith(DOC_EXCLUDES) and
                name not in DOC_INCLUDE_EXCEPTIONS):
            continue
        if (not is_stable_reader_document(name) and
                name not in DOC_STRUCTURE_PATHS):
            continue

        path = os.path.join(ROOT, name.replace("/", os.sep))
        try:
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
        except (IOError, UnicodeError):
            # The link/encoding audit reports the detailed read error.
            continue

        checked += 1
        lines = text.splitlines()
        limit = DOC_ROLE_TEXT_LIMITS.get(name)
        normalized_size = normalized_text_size(text)
        if limit is not None and normalized_size > limit:
            errors.append(
                "%s exceeds its %d-character role-size tripwire "
                "(%d non-whitespace characters)" %
                (name, limit, normalized_size))

        for start, prose in iter_markdown_prose_blocks(lines):
            prose_size = normalized_text_size(prose)
            if prose_size > DOC_PROSE_BLOCK_LIMIT:
                errors.append(
                    "%s:%d has an oversized semantic prose block "
                    "(%d non-whitespace characters)" %
                    (name, start, prose_size))

        if is_stable_reader_document(name):
            match = batch_re.search(text)
            if match is not None:
                line = text.count("\n", 0, match.start()) + 1
                errors.append("%s:%d contains a per-next batch reference" %
                              (name, line))
            match = run_re.search(text)
            if match is not None:
                line = text.count("\n", 0, match.start()) + 1
                errors.append("%s:%d contains a dated local device run" %
                              (name, line))

        headings = set()
        in_fence = False
        for number, line_text in enumerate(lines, 1):
            stripped = line_text.strip()
            if stripped.startswith("```") or stripped.startswith("~~~"):
                in_fence = not in_fence
                continue
            if not in_fence:
                heading = heading_re.match(line_text)
                if heading is not None:
                    normalized = heading.group(1).strip().lower()
                    if normalized in headings:
                        errors.append("%s:%d repeats heading '%s'" %
                                      (name, number,
                                       heading.group(1).strip()))
                    headings.add(normalized)
                if (name == ".agents/ROADMAP.md" and
                        roadmap_heading_re.match(line_text)):
                    errors.append(
                        "%s:%d has a completed/batch roadmap heading" %
                        (name, number))
    return checked


def main():
    errors = []
    tracked = load_tracked()
    worktree_files = load_worktree_files()

    for name in REQUIRED:
        path = os.path.join(ROOT, name.replace("/", os.sep))
        if not os.path.isfile(path):
            errors.append("missing required file %s" % name)
        elif tracked is not None and name not in tracked:
            errors.append("required file is not tracked: %s" % name)

    project_count = 0
    source_count = 0
    for project in iter_projects():
        project_count += 1
        source_count += audit_project(project, tracked, errors)

    host_source_count = audit_test_host_boundary(errors)
    audit_versions(errors)
    structured_document_count = audit_document_structure(worktree_files,
                                                          errors)
    document_count, link_count = audit_markdown_links(worktree_files, errors)

    if errors:
        print("Repository audit FAILED (%d issue(s)):" % len(errors))
        for error in errors:
            print("  - " + error)
        return 1

    tracked_note = "tracked" if tracked is not None else "existence-only"
    print("Repository audit OK: %d projects, %d project inputs, %s mode." %
          (project_count, source_count, tracked_note))
    print("Documentation audit OK: %d files, %d local links." %
          (document_count, link_count))
    print("Documentation structure OK: %d role-governed files." %
          structured_document_count)
    print("test_host boundary OK: %d source files checked." %
          host_source_count)
    print("Pinned sources: Mbed TLS 2.16.12, cJSON 1.7.18.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
