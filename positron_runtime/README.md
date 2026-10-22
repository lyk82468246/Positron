# positron_runtime.dll

Runtime is the public owner of the resource-application contract described in
[the approved plan](../docs/RUNTIME_PLAN.md). The implemented foundation admits
manifest metadata, canonical application IDs, decoded package-relative paths and
lexical `app://` references.
It does **not yet run directory applications**: directory binding, exclusive
instances, platform resource reads, SQL services and PXI
installation are not implemented by this foundation.

## Link and deploy

Include `positron_runtime.h`, link `positron_runtime.lib`, and deploy
`positron_runtime.dll` with the matching `positron_json.dll` from one formal
Debug or Release stage. Build through the root solution, never compile Runtime
or cJSON implementation sources into a consumer. The DLL performs no file,
network, window, SQL or worker operations during metadata admission.

## Manifest v1

All eight fields are required, case-sensitive, and unknown/duplicate fields fail
closed. UTF-8 input is limited to 8192 bytes, eight container levels and 256
lexical tokens including keys/containers; strict grammar/decoding belongs to
`PJson_ParseEx`, not a second Runtime JSON parser. BOM, trailing material,
embedded/decoded NUL, invalid UTF-8 and nonfinite numbers are rejected.

```json
{
  "manifestVersion": 1,
  "id": "com.example.notes",
  "name": "Notes",
  "version": 1,
  "runtimeApiVersion": 1,
  "entry": "index.html",
  "permissions": ["storage.sql"],
  "migrations": [{"version": 1, "file": "schema/1.sql"}]
}
```

`manifestVersion` and `runtimeApiVersion` must be numeric integer 1; other valid
versions return UNSUPPORTED. Application `version` is a positive integer up to
2147483647, not semver text or a file timestamp. `name` is 1–127 UTF-8 bytes,
without C0/C1/DEL controls. Empty permissions/migrations arrays are valid;
the only accepted permission is exactly `storage.sql`. Unknown permissions fail
closed, duplicates are invalid. A requested permission is **not a grant**.

At most sixteen migrations are accepted. Each has exactly `version` and `file`;
schema versions are contiguous from 1, file paths cannot repeat under ASCII
case-insensitive comparison, and migrations require the SQL permission declaration.
This only validates the migration catalog; it does not read or execute SQL,
validate installed schema, or authorize legacy local-full/migration APIs for JS.

## IDs and paths

`PRuntime_NormalizeId` accepts explicit-length IDs of at most 63 ASCII bytes:
at least two dot-separated segments, each letter-led followed by letters,
digits or underscores. It lowercases ASCII only. `Com.Example.Notes` and
`com.example.notes` are the same logical identity; `org.demo_app.v2` is valid.
Single segments, empty segments, digit/underscore-led segments, hyphens, Unicode,
percent encoding, ports and whitespace are rejected. IDs are not DNS hostnames
or device paths and are not directly concatenated into filesystem names.

Entry and migration paths use `PRuntime_ValidateResourcePath`: decoded UTF-8,
1–255 bytes, at most sixteen nonempty slash-separated segments. Leading/trailing
slash, dot/dotdot, backslash, colon, percent, query/fragment, wildcard/quote,
angle/pipe characters, controls and segment-final dot/space are rejected.
This is a lexical package key, not a file-opening or containment guarantee.
Do not percent-decode it or treat it as an absolute path. Physical directory
identity, alias/case resolution, containment and access remain prerequisites for
the later directory-instance contract. URL references including query/fragment
use the resolver below, not this path validator.

## Lexical app references

`PRuntime_ManifestResolveReference` takes a manifest, an optional absolute same-ID
base URL, an explicit-length reference and an exact size/version
`PRuntimeResourceReference` output. With no base it starts at the manifest entry.
An empty reference is valid. This pure operation never opens a resource, binds a
directory, creates an instance lock or grants service authority.

Relative paths, root paths, dot segments within the package, query-only,
fragment-only, same-ID `app://` and network-path references are supported. `/`
and an authority with no path map to the manifest entry; other directory-only
references have no implicit index-file lookup. A base must explicitly use `app://`,
not a relative or network-path spelling. Authorities normalize through the ID
contract, including underscores; different IDs are rejected. External schemes
return UNSUPPORTED and are not sent to HTTP or the filesystem by this DLL.

Input URLs and canonical output are each at most 1023 bytes. The returned `path`
is a once-decoded UTF-8 package key under the existing 255-byte/16-segment budget.
Raw controls, spaces and backslashes, malformed escapes, encoded path separators,
dot bytes, percent and native-path syntax fail closed. Raw `.`/`..` segments
normalize without escaping the package root. Percent-encoded UTF-8 and interior
spaces in filenames are accepted; neither the host nor a later callback may
decode the returned key again. These lexical rules still do not prove physical
containment or exclude filesystem aliases.

`url` is a canonical escaped navigation URL. `query` and `fragment` omit their
delimiters and are each limited to 255 escaped bytes; flags distinguish absent
from present-but-empty suffixes. Reserved query delimiters remain distinct from
escaped octets (`&` is not `%26`); hex escapes normalize to uppercase. Fragment-only
or empty references inherit the base query, never its fragment; a new path drops
the old query. Neither suffix belongs in a file-open key. Every failure preserves
the complete output; repeated resolution does not mutate manifest state.

```c
PRuntimeResourceReference resource;
resource.size = sizeof(resource);
resource.version = PRUNTIME_VERSION;
rc = PRuntime_ManifestResolveReference(manifest, NULL, 0,
    "assets/main.js", 14, &resource);
/* resource.path is a lexical key only. Do not open it without a separately
 * verified trusted instance and the forthcoming resource-read contract. */
```

## C lifecycle and failure contract

```c
PRuntimeManifestHandle manifest;
PRuntimeManifestInfo info;
int rc;

manifest = NULL;
rc = PRuntime_ManifestCreate(bytes, byte_count, &manifest);
if (rc == PRUNTIME_OK) {
    info.size = sizeof(info);
    info.version = PRUNTIME_VERSION;
    rc = PRuntime_ManifestGetInfo(manifest, &info);
    /* Copy metadata into a host-owned confirmation model, not a grant. */
    PRuntime_ManifestClose(&manifest);
}
```

Create borrows input only until return and owns an immutable copied snapshot.
Use `ManifestGetInfo` and indexed `ManifestGetMigration` with exact v1
size/version outputs. Failures do not modify output buffers/handle slots.
Close takes the owned slot and clears it; repeated NULL values are safe,
but a NULL slot pointer is ARGUMENT. Never reuse aliases after close. Keep each
handle on one owner thread; no callbacks survive return because none occur.

ARGUMENT is an invalid API contract, LIMIT is a fixed capacity violation,
INVALID is malformed metadata/path/JSON (including a JSON parser allocation
failure that its upstream API cannot distinguish), UNSUPPORTED is a recognized
but unavailable format/API/permission, MEMORY is failure allocating the Runtime
snapshot. No failed call creates a partial handle or performs I/O.

Ordinary pages, forged `app://` strings, or possession of this metadata handle
have no service authority. A future explicit trusted instance must bind the
canonical directory/installation and logical ID, isolate development/installed
data, hold the exclusive lock and authorize each session lifecycle. There is no
malicious-code sandbox or verified publisher claim. EXE integration must wait
for that contract rather than invent private grants or URL routing rules.

## Validation boundary

The offline test host exercises canonical IDs, paths, strict JSON, manifest
field/version/permission/migration rejection, copied snapshots, app reference
normalization, encoded traversal/authority/capacity rejection, suffix separation,
failed-output canaries and repeated parse/query/resolve/close. Device evidence belongs to the current
handoff, not this stable document. These tests do not prove directory I/O,
physical app resource reads, SQL authorization, PXI/ZIP or complete browser UI integration.
