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

# These documents have deliberately narrow roles.  Generous line budgets are
# not style targets; they are tripwires against quietly rebuilding a per-next
# ledger in a reader guide or one-page agent snapshot.
DOC_ROLE_LINE_LIMITS = {
    "README.md": 300,
    "docs/ARCHITECTURE.md": 700,
    "docs/TESTING.md": 700,
    ".agents/HANDOFF.md": 250,
    ".agents/KNOWN_LIMITATIONS.md": 300,
    ".agents/ROADMAP.md": 250,
    "positron_browser/README.md": 300,
    "positron_core/README.md": 260,
    "test_host/README.md": 260,
}

DOC_STRUCTURE_PATHS = set(DOC_ROLE_LINE_LIMITS)
DOC_STRUCTURE_PATHS.update((
    "AGENTS.md",
    "docs/README.md",
    "docs/BUILDING.md",
    "docs/TROUBLESHOOTING.md",
    "docs/NIGHTLY_RELEASE.md",
    ".agents/README.md",
    ".agents/DEBUGGING.md",
))


def relpath(path):
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


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
        limit = DOC_ROLE_LINE_LIMITS.get(name)
        if limit is not None and len(lines) > limit:
            errors.append("%s exceeds its %d-line role budget (%d lines)" %
                          (name, limit, len(lines)))

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
            if not in_fence and not stripped.startswith("|") and len(line_text) > 240:
                errors.append("%s:%d has a %d-character prose line" %
                              (name, number, len(line_text)))
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
    print("Pinned sources: Mbed TLS 2.16.12, cJSON 1.7.18.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
