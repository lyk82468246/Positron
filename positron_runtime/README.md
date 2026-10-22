# positron_runtime.dll

Runtime is the public owner of the resource-application contract described in
[the approved plan](../docs/RUNTIME_PLAN.md). The implemented foundation admits
manifest metadata, canonical application IDs, decoded package-relative paths and
lexical `app://` references.
Development-directory instances bind a trusted platform provider, admit that
root's manifest, hold an exclusive launch lease and read bounded resources.
An opt-in migration plan copies the declared pending SQL files after an explicit
native permission decision; it does not execute them or select a database.
Development storage now binds an exact private DB identity, runs admitted
migrations and executes bounded typed SQL JSON on its owner worker.
This does **not yet run directory applications in the browser**: the general WM
provider/EXE integration, installed identities, asynchronous session authorization,
Core/Browser app resource wiring and PXI remain separate prerequisites. The native
SQL service queue below now supplies bounded cross-thread delivery and revocation;
it is not EXE integration or permission inferred from an address.

## Link and deploy

Include `positron_runtime.h`, link `positron_runtime.lib`, and deploy
`positron_runtime.dll` with the matching `positron_json.dll` and `positron_db.dll` from one formal
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
identity, alias/case resolution, containment and access are supplied by the
trusted directory provider described below. URL references including query/fragment
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
 * verified trusted instance and its resource-read contract. */
```

## Trusted development-directory instances

`PRuntime_DevelopmentDirectoryOpen` is an explicit native development-launch
entry, not a URL handler or an installed-app opener. It copies a v1
`PRuntimeDirectoryProvider` table, retains the provider's canonical root, reads
`manifest.json` from that root under the existing 8192-byte limit and admits it
through the same manifest parser. It then acquires an exclusive lease for the
development namespace, exact canonical directory identity and normalized ID.
Caller metadata cannot substitute for the root manifest. The returned instance
does not grant SQL, Browser services or access to an installed app's data.

The host owns native operations, not application rules. Its provider must:

- Resolve the real directory and produce a stable, case/alias-canonical UTF-8
  identity (1–1023 bytes), retaining an opaque root for subsequent reads. Merely
  copying the launch string or checking a path prefix is not sufficient.
- Reject unprovable containment, links/aliases that escape the retained root,
  unavailable storage and permission failure. Never switch roots or silently
  fall back to another volume. The root/resources must remain stable until close;
  this interface is not a mutable-directory watcher or malicious-code sandbox.
- Acquire an atomic cross-process lease for the exact opaque ASCII lock name
  Runtime supplies. An existing live name is BUSY, including another launch in
  the same process. A WM provider can retain a newly created named kernel object
  and reject `ERROR_ALREADY_EXISTS`; it must not wait, add a PID suffix, or mistake
  existing/unowned for exclusive admission.
- Read only the decoded relative key against the retained root, returning all
  immutable bytes plus a release token synchronously. Enforce the supplied byte
  limit before allocation/read and independently prove physical containment.
  Query/fragment never reach this callback; no URI re-decoding or HTTP fallback.

The finite noncryptographic lock bucket can collide. A collision only rejects
simultaneous admission as BUSY; it never merges identities, authorizes a service
or selects storage. **Never use the lock name as a persistent database/data key.**
The general WM filesystem provider is still a host integration requirement;
the test fixture uses known private roots and is not a reusable canonicalizer.

Use `PRuntime_DirectoryGetInfo` for the copied manifest snapshot and
`PRuntime_DirectoryResolveReference` for the bound ID's lexical references.
`PRuntime_DirectoryReadResource` resolves a reference and copies binary bytes
into a caller-owned buffer. Capacity 1–8 MiB is the read limit, not a size probe;
obey stricter Core/Browser resource limits. Zero-byte files are valid, no NUL is
appended, and buffer/count/resolved metadata all remain unchanged on failure.
This is not proof that an HTML entry parses or that every asset exists.

All instance operations run on the creating thread; callbacks are synchronous
and non-reentrant, with no UI pump, hidden thread or asynchronous continuation.
Live-instance operations from its callback or another thread return STATE,
including Close without clearing/freeing the slot. Open itself must not be
recursively called by a provider. When using a worker, create/use/close the
instance there and exchange copied requests/results with the UI; never move the
live handle, Core document or ScriptSession between threads.

Every non-NULL byte token is released before return, including callback failure
and oversized/malformed results. Failed Open unwinds manifest, lease and root;
Close uses the same order and clears the owned slot. Native release callbacks
must complete without failure, reentry or exceptions. Callback code and the
`user_data` target must remain valid through Close; the original table can be
discarded after Open. Allowed provider failures
are IO, LIMIT, UNSUPPORTED and MEMORY, plus BUSY for locking; unexpected statuses
or malformed success results become IO. The DLL has no cross-thread cancel or
hard I/O deadline: the host must bound its platform operations.

```c
PRuntimeDirectoryHandle instance;
PRuntimeResourceReference resource;
unsigned int count;
char* html;

instance = NULL;
html = (char*)malloc(65536); /* include <stdlib.h>; illustrative consumer limit */
if (html != NULL) {
    rc = PRuntime_DevelopmentDirectoryOpen(directory_utf8, directory_bytes,
        &trusted_provider, &instance);
    if (rc == PRUNTIME_OK) {
        resource.size = sizeof(resource);
        resource.version = PRUNTIME_VERSION;
        rc = PRuntime_DirectoryReadResource(instance, NULL, 0, NULL, 0,
            html, 65536, &count, &resource); /* entry, explicit byte length */
        /* Parse using count only after successful read; do not expose native
         * roots or grant Browser services merely because Open/Read succeeded. */
        PRuntime_DirectoryClose(&instance);
    }
    free(html);
}
```

## Preparing declared migration sources

`PRuntime_DirectoryPrepareMigrations` admits an immutable source plan from a live
development-directory instance. Supply exact v1 `PRuntimeMigrationPlanOptions`:
`granted_permissions` is a trusted native confirmation decision, not an author
field; `current_schema_version` must come from the strict DB version reader on
the correctly isolated application database, not JS or the manifest. The SQL bit
must be both granted and declared. Missing either returns DENIED; unknown grant
bits return UNSUPPORTED, before reading any SQL file. A stored version beyond
the manifest catalog returns SCHEMA, never a downgrade or an automatic reset.

Only catalog entries from `current_schema_version + 1` through the target are
read, in order, using their already-validated decoded keys and the retained
provider root. There is no caller-supplied SQL path or URL resolver fallback.
Each file is nonempty UTF-8, at most 32768 bytes, without BOM, NUL or controls
other than TAB/CR/LF. Up to sixteen files occupy at most 512 KiB of copied SQL
plus fixed metadata; temporary provider bytes are released per file. Whitespace
or syntactically invalid SQL can pass this text admission: the DB authorizer,
parser and work budget remain authoritative for executable SQL. This operation
neither executes nor partially applies any migration.

The handle is published only after every pending source was read and copied.
Missing, oversized, malformed or failed later files release all earlier copies
and preserve the caller's output slot. An up-to-date schema produces a valid
zero-file plan without resource reads. Provider callbacks cannot reenter or
close the directory; the existing physical-containment, immutable-resource and
no-hard-I/O-deadline contracts still apply.

Query exact v1 `PRuntimeMigrationPlanInfo` for normalized ID, previous/target
versions and counts. `PRuntime_MigrationPlanGetSource` uses a zero-based pending
index, returning the catalog version, file key and borrowed immutable SQL plus
explicit byte length. SQL is **not NUL terminated**. Source pointers last until
`PRuntime_MigrationPlanClose`, which clears the owned slot and permits repeated
NULL values. All plan calls use the creating thread. The plan owns no directory
or provider references and remains readable after the directory closes; this
is copied source data, **not a live authority, lease or database identity**.

```c
PRuntimeMigrationPlanHandle plan;
PRuntimeMigrationPlanOptions options;
PRuntimeMigrationSource source;
plan = NULL;
memset(&options, 0, sizeof(options)); /* include <string.h> */
options.size = sizeof(options);
options.version = PRUNTIME_VERSION;
options.granted_permissions = trusted_sql_confirmation;
options.current_schema_version = strict_db_version;
rc = PRuntime_DirectoryPrepareMigrations(instance, &options, &plan);
if (rc == PRUNTIME_OK) {
    source.size = sizeof(source);
    source.version = PRUNTIME_VERSION;
    /* For each pending index, get source and use its explicit sql_bytes.
     * The Runtime storage owner below binds the right DB and applies via
     * PDb_ApplyMigrationRestricted(expected, expected+1), rechecking version
     * under DB's write lock. Do not use legacy full-SQL migration for JS. */
    PRuntime_MigrationPlanClose(&plan);
}
```

This source-only API still accepts no arbitrary DB handle. Use the development
storage contract below for identity binding and execution. Individual DB migration
transactions do not make a multi-file catalog atomic, and neither layer claims
physical power-loss durability.

## Isolated development storage and migrations

`PRuntime_DevelopmentStorageOpen` takes a live directory, explicit native SQL
confirmation, per-file/request work budget (100–1000000, multiples of 100),
optional cooperative cancel callback and a copied native storage provider.
Missing declaration/confirmation fails before native I/O. The provider anchors
to the real EXE-adjacent `data` directory and opens Runtime's relative key below
that root, proving native containment and retaining the volume/path through
close. It must not alias the browser DB, choose another root on failure, delete
files, or use author-supplied database paths.

Runtime owns a `v1/dev/<16-lowercase-hex-bucket>/app.db` mapping. This finite,
noncryptographic bucket is only a location hint, separate from the launch lock.
Before migrations, `PDb_BindLocalIdentity` atomically compares a full framed
development-kind/normalized-ID/canonical-root TEXT identity in private DB
metadata. A collision or wrong mapped file returns SCHEMA; an unclaimed nonempty
DB is never adopted. Thus equal IDs in different roots remain separate, and a
development application cannot claim an installed identity. Installed open/update
is not implemented; the installed namespace must never alias `v1/dev`.

The owner reads strict schema version, copies every pending manifest source,
then applies each with `PDb_ApplyMigrationRestricted` in order. Invalid/missing
later source stops before any migration SQL; initial mode/identity metadata may
already exist. Each file's DDL/DML/version commits together. A later SQL failure
keeps earlier committed versions and blocks author startup; normal retry resumes
only the pending suffix, not a replay of earlier files. Future/corrupt versions
never trigger downgrade, reset or deletion. Cancellation cannot undo a COMMIT or
preempt native I/O, and the catalog is not one atomic transaction.

Directory, storage and DB live on one creating worker. A storage child retains
its parent: directory Close returns STATE until storage Close. There is one
storage child per directory; native callbacks cannot reenter/close an active
instance. Storage Close releases DB before the provider location, then permits
parent release. Callback code/user targets must remain live throughout. No DLL
worker or window message pump is created, and no raw DB handle is exposed.

`PRuntime_StorageGetInfo` copies ID, native-only key, schema version and state.
READY is required for SQL. A request failure leaving an active DB transaction
latches FAILED until close, with no implicit recovery/delete. Optional v1 open
error and `PRuntime_StorageGetError` copy structured Runtime/DB/category/native/
cleanup diagnostics; successful operations preserve a previous failure. Invalid
query headers/owner guards do not replace that diagnostic. Native messages may
contain private details: do not forward them blindly to author JS.

## Typed worker SQL JSON

`PRuntime_StorageExecuteJson` is a synchronous worker primitive, not a Browser
service registration or permission inferred from URL. Version 1 requests are
strict UTF-8 JSON, at most 4096 bytes, eight container levels and 768 tokens;
unknown/duplicate fields, unknown methods/types and future versions fail closed.

```json
{"version":1,"method":"query","sql":"SELECT id,body FROM notes WHERE id>? LIMIT 16","params":[{"type":"int64","value":"9007199254740993"}]}
```

`execute` has the same four keys. A transaction instead has exactly `version`,
`method:"transaction"` and a nonempty `statements` array, with up to eight objects
having exactly `kind:"query"|"execute"`, `sql` and `params`. Each parameter is
`{"type":"null"}` or `{"type":"text"|"real"|"int64","value":...}`.
int64 is canonical decimal string across the full signed 64-bit range; signs
other than minus, leading zeros, negative zero, numbers or overflow are rejected.
`real` is a finite JSON number and `text` a UTF-8 string. No BLOB or embedded NUL.

DB retains SQL authority and its budgets: SQL 2048 bytes per statement, sixteen
binds/columns/total rows, safe functions, main query/DML only, no private metadata,
DDL or caller transaction/connection controls. The shared request work budget
does not reset between batch statements. Query results exceeding sixteen rows
fail rather than truncate; callers should use bound cursor/LIMIT pagination.

Success returns `version`, `statementCount`, `rowCount`, typed decimal `changes`,
and `rows:[{"statement":0,"values":[typed values...]}]`. The statement index is
zero-based within the request. Values mirror input type/value tags; column
names are not guessed. JSON owns scalar escaping/finite-number encoding, so
quote/newline/Unicode round-trip without int64 conversion to JS Number.

The caller must provide at least 4097 output bytes **before execution**. There
is no side-effecting size probe. Runtime buffers encoded rows before DB COMMIT,
reserving 128 bytes for a fixed report; overflow, unsupported NUL/BLOB, encoding
allocation failure, SQL error or cancellation rolls back the whole request.
Only after COMMIT does it append the allocation-free report and copy at most
4096 bytes plus NUL; `written` excludes NUL. All failed outputs stay unchanged.
The fixed report reserve may conservatively reject a near-limit row payload.

The host must schedule copied requests/results, never call this on UI or move
live storage/Core/Browser/JS handles to another thread. Use the explicit queue
below for asynchronous delivery; this worker entry alone does not make a web page trusted.

## Bounded page SQL service queue

Create `PRuntime_ServiceQueueCreate` on the storage owner worker, after migrations
and native confirmation succeed. Exact v1 options bind nonzero tab/page generation
and UI delivery thread. At most four independent queues share one storage, allowing
an old page and candidates to coexist; duplicate tab/generation is STATE. Each
queue retains storage until close. Its fixed allocation is approximately 70 KiB
(280 KiB for four), with eight slots including waiting, running and unclaimed results.

This queue is an explicit cross-thread **copy channel**, not permission to move
directory/storage, Core, Browser or JS handles. On UI, register only `storage.sql`
through Browser's controlled service bridge after bootstrap completes and before
author code. Copy request tags/method/JSON into `PRuntime_ServiceQueueSubmit`, then
wake the worker. Submit checks tags and the 4096-byte copy budget, but performs no
JSON parsing, SQL, native I/O or callback. The first accepted bridge ID binds the
queue; accepted request IDs must strictly increase, including after consumption
or cancellation, so a previously accepted token cannot replay a write.

The worker calls `PRuntime_ServiceQueueProcessOne` for one FIFO request. SQL failure
is a completed result, not failure of this processing API. UI calls
`PRuntime_ServiceQueueTakeResult`, verifies live tab/generation, then uses Browser
Complete and Pump on the owning UI thread. Results are consumed exactly once;
empty returns NOT_FOUND without modifying output. Errors contain only numeric
Runtime/DB/category fields, never private SQL, paths or diagnostic messages.
The host owns wakeups, fairness and Browser callback scheduling; the DLL creates
no thread, timer or message pump. Direct StorageExecuteJson is STATE while any
queue owns that storage, preventing an alternate path around queue ownership.

Browser cancel callbacks call QueueCancel on UI. Before destroying a session,
revoke Browser services and permanently revoke its Runtime queue: waiting/results
are dropped and running SQL receives cooperative cancellation. A cancel/revoke
racing COMMIT may discard an already committed result; **never reexecute SQL to
recover that result**. VFS/provider calls are not preempted. Keep the old page's
queue valid until actual replacement, rather than revoking it when a candidate
starts. Candidate queues have independent tags and no access to the old session.

Stop UI access before worker-only QueueClose, which requires revoked and idle.
Close all queues before storage, storage before directory; join the worker before
freeing its state or unloading DLLs. GetInfo is a copied snapshot callable only
from the worker or specified delivery thread. As with other native handles,
freed aliases are invalid; the queue does not make a stale Browser pointer safe.

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
snapshot. No failed metadata call creates a partial handle or performs I/O.
Directory calls add IO for provider/unavailable-resource errors, BUSY for an
existing lease and STATE for wrong-thread/active-instance operations; they may
perform balanced provider acquisition and release before returning failure.
Migration preparation adds DENIED for a missing declaration/native decision and
SCHEMA for a newer stored version than the admitted catalog; these fail before
SQL reads and do not change stored data or authorize a service.
Storage adds DATABASE for DB/authorizer/constraint failures and CANCELLED for a
cooperative cancellation. Inspect structured diagnostics rather than parsing
English error messages or deleting a failed database.

Ordinary pages, forged `app://` strings, or possession of this metadata handle
have no service authority. Development instances bind the provider's canonical
root and logical ID, not installed storage. Session integration must still
authorize each lifecycle and keep installed namespaces separate. There is no
malicious-code sandbox or verified publisher claim. EXE integration must wait
for that contract rather than invent private grants or URL routing rules.

## Validation boundary

The offline test host exercises canonical IDs, paths, strict JSON, manifest
field/version/permission/migration rejection, copied snapshots, app reference
normalization, encoded traversal/authority/capacity rejection, suffix separation,
failed-output canaries and repeated parse/query/resolve/close. The directory
fixture adds known private Unicode roots, native binary/empty reads, same-ID
root isolation, same-root provider aliases, a second-process duplicate attempt,
owner/reentry, partial acquisition failure and repeated teardown. The migration
fixture checks exact source order/bytes, missing
grants, version rejection, later-file failure, malformed/oversized input,
16-file/512-KiB limits, owner-thread teardown and public DB primitive composition.
Storage fixtures additionally prove native private DB reopen, exact identity
mismatch rejection, per-file migration failure/resume, all-source admission
before DDL, typed SQL/batch rollback, encoded overflow, cancellation and budget
guards. A real UI/worker fixture composes the Browser bridge with eight-slot
backpressure, four page contexts, duplicate/replay rejection, running cancellation,
rollback and post-COMMIT stale-result disposal. None proves a generic filesystem
canonicalizer, installed launch, EXE authorization, PXI/ZIP or complete browser UI integration. Device evidence belongs
to the current handoff, not this stable document.
