# positron_db.dll

`positron_db.dll` provides a bounded SQLite database boundary for Positron.
It exposes a stable UTF-8 C ABI with opaque database and statement handles,
typed bind/column access, transactions and application-owned migrations.

`PDb_BindBlob(stmt, index, NULL, 0)` binds an empty BLOB, not SQL NULL;
use `PDb_BindNull()` for NULL. Authorization denials return
`PDB_SQL_REJECTED`. `PDb_Cancel()` is owner-thread-only: the next `PDb_Exec()`
or unfinished `PDb_Step()` consumes the pending request before execution and
returns `PDB_STATE`; later calls remain usable. It does not allow sharing a
handle with another thread.

## Errors and transaction state

Existing integer return codes remain unchanged. `PDb_OpenUtf8Ex()` optionally
copies an open-failure snapshot even when no DB handle was created;
`PDb_GetErrorInfo()` copies the last failure for an existing handle. Initialize
the caller-owned output before either call:

```c
PDbErrorInfo error;
error.size = sizeof(error);
error.version = PDB_ERROR_INFO_VERSION;
```

The snapshot contains the public result, machine-readable category, SQLite
primary/extended codes, bounded UTF-8 message, transaction state at failure
return and any automatic rollback result. Wrapper-only failures have zero
SQLite codes. Insufficient size or an unknown version leaves the output
unchanged; a larger buffer is accepted without touching its extension bytes.
The snapshot owns its message and requires no allocation or free operation.

Errors are sticky: successful calls, column sentinel getters and diagnostic
queries preserve the last failure. A failing status operation with a live owner
replaces it, including parameter errors; `PDb_ClearError()` explicitly resets
it without changing SQL or transaction state. There is no thread/process-global
last-error slot. NULL handles cannot retain diagnostics; using closed/freed
handles remains invalid, not a supported stale-handle query.

`PDb_GetConnectionState()` uses its own size/version contract and reports live
SQLite autocommit, main-database read/write transaction state and outstanding
statement count. It differs from the frozen error snapshot: COMMIT BUSY or a
deferred constraint failure can leave a transaction active, while some execution
errors roll it back. Transactions begun through local SQL follow the same live
state as `PDb_Begin()`. The caller must check state before retrying or rolling
back, rather than infer it from a failed return.

Migrations own their transaction in both modes; scripts cannot execute
transaction/savepoint, attachment, PRAGMA or extension-loading operations.
Script, metadata or commit failure does not advance the stored schema version.
Automatic cleanup attempts rollback while preserving the original diagnostic;
`cleanup_attempted`/`cleanup_result` and live state distinguish attempted cleanup
from successful rollback. An I/O/corruption category does not authorize deleting
the database or guarantee that it is safe to retry. Real storage faults and
power-loss recovery need separate device validation.

## Opt-in restricted requests

`PDb_ExecuteRestricted()` is the DB prerequisite for the resource-application
runtime in [the approved plan](../docs/RUNTIME_PLAN.md). It is not an implicit
restriction on existing local-full consumers, nor permission to expose a
browser database to a page. Runtime must select an isolated application DB,
validate its identity/permissions and provide the asynchronous service layer.
Those Runtime and EXE integrations are separate work.

The caller supplies v1 size/version options and report, an array of up to eight
explicit-length statements, typed positional parameters and optional callbacks.
Use the DB only on its owner worker. The connection must be local-full, idle,
without outstanding statements or an enclosing transaction. The call owns one
transaction and publishes the report only after COMMIT. Errors leave the report
unchanged and attempt rollback using the sticky error/cleanup contract above.

The authorizer is active during both compilation and execution, including view
and trigger access. It permits main-database SELECT and ordinary INSERT/UPDATE/
DELETE, not DDL, temporary/attached databases, PRAGMA, extension loading, host
transaction/savepoint control or `__pdb_`/`sqlite_` objects (ASCII case-insensitive).
A statement must actually receive query/DML authorization; maintenance operations
such as VACUUM cannot bypass that test. Unpreparable SQL also returns
`PDB_SQL_REJECTED`, with its original SQL diagnostic; actual authorizer denials
use AUTHORIZATION. There is no keyword filtering or error-message interpretation.

Allowed functions are abs/avg/char/coalesce/count/date/datetime/format/glob/hex/
ifnull/iif/instr/julianday/length/like/likelihood/likely/lower/ltrim/max/min/nullif/
printf/quote/replace/round/rtrim/sign/strftime/substr/substring/sum/time/total/trim/
typeof/unicode/unixepoch/unlikely/upper/changes/last_insert_rowid. Schema comes
from separate trusted migration calls, never an author-supplied service request;
this contract does not authenticate a manifest or establish a malicious-code
sandbox. Unknown/unavailable functions fail rather than extending the allowlist.

Limits are 2048 UTF-8 SQL bytes per statement, 16 exact-index binds, 16 columns,
16 total result rows and 4096 aggregate result-text bytes. QUERY must be readonly;
EXECUTE must not return columns, so DML RETURNING is rejected. NULL, int64,
finite real and length-delimited UTF-8 text are supported; BLOB/nonfinite results
are unsupported. A bound text value is limited to 4096 bytes. SQL cannot contain
embedded NUL or extra statements; trailing whitespace is allowed. Nothing is
silently truncated. Runtime must separately enforce its 4096-byte request/result
JSON limit, preserve int64 as typed decimal strings and reject JSON overflow.

The shared work budget is 100–1000000 in multiples of 100. Each statement reserves
100 units for a partial checkpoint; further SQLite progress checkpoints charge
100 units. This is conservative work accounting, not exact profiler instruction
counts or a hard wall-clock/VFS/memory bound. SQLite may also call progress during
compilation. Budget exhaustion is `PDB_LIMIT`; cancellation is
`PDB_STATE` with CANCELLED category. The borrowed cancel callback only reads a
host atomic flag; the UI never calls a DB handle across threads. Cancellation
is checked before work, at checkpoints/row boundaries and before COMMIT; it
cannot undo a transaction already committed or preempt a blocking VFS operation.
The old owner-thread `PDb_Cancel()` contract remains unchanged.

```c
PDbRestrictedOptions options;
PDbRestrictedReport report;
PDbRestrictedStatement query;
int rc;

memset(&options, 0, sizeof(options));
options.size = sizeof(options);
options.version = PDB_RESTRICTED_VERSION;
options.vm_budget = PDB_RESTRICTED_VM_MAX;
options.cancelled = read_atomic_cancel; /* no DB calls or UI pumping */
options.row = buffer_provisional_row;  /* copy values; return LIMIT if full */
options.user_data = request;
memset(&report, 0, sizeof(report));
report.size = sizeof(report);
report.version = PDB_RESTRICTED_VERSION;
memset(&query, 0, sizeof(query));
query.sql = "SELECT id, body FROM notes ORDER BY id LIMIT 16";
query.sql_bytes = (int)strlen(query.sql);
query.kind = PDB_RESTRICTED_QUERY;
rc = PDb_ExecuteRestricted(db, &query, 1, &options, &report);
/* Only rc == PDB_OK permits publishing buffered results after identity and
   generation checks. Otherwise discard them and inspect PDb_GetErrorInfo(). */
```

Rows are synchronous, borrowed and **provisional**: a later constraint, overflow,
cancel or COMMIT failure invalidates all of them. The callback must copy to its
private bounded buffer, not send JS/UI messages or perform external effects;
return `PDB_LIMIT` for result overflow, another nonzero value for state rejection.
Missing row callback rejects any nonempty result. Same-DB status reentry refuses
STATE and callback Close cannot free the active request. Do not retain pointers,
block, pump messages or invoke cleanup from either callback. No callback survives
return. The report counts committed statements/rows and total changes including
triggers; it is not a filesystem durability or retry guarantee.

Deploy the matching DB DLL and public header/import library from one formal
configuration; its existing JSON DLL dependency is unchanged. Legacy local,
sync, migration, error and transaction APIs keep their ABI and behavior.

## Opt-in manifest migration primitive

`PDb_GetMigrationVersion()` and `PDb_ApplyMigrationRestricted()` are separate
DB prerequisites for Runtime-managed manifest migrations. They do not select
an application database, authenticate an identity, read migration files or grant
a Browser/JS service. Runtime must still bind the isolated storage identity,
check permissions and load only its admitted manifest catalog. Do not substitute
the older local-full `PDb_ApplyMigration()` for this boundary.

The getter accepts only an idle local-full connection. A missing schema version
is zero; an existing value must be TEXT containing canonical decimal 0–2147483647.
Signs, leading zeros, embedded NUL, BLOB, overflow and overlong values return
SCHEMA_MISMATCH without changing the output. No truncated C-string read is used.

The migration API takes one explicit-length UTF-8 file (1–32768 bytes), expected
and target versions, v1 `PDbRestrictedOptions` with `row=NULL`, and a v1
`PDbMigrationReport`. Target must equal expected+1. It obtains BEGIN IMMEDIATE
before comparing the stored version, so a version changed by another writer
cannot be silently overwritten. A stale/future/corrupt stored version fails
closed; the caller must stop rather than downgrade, delete the database or
replay a failed write automatically.

The v1 schema subset is main-database CREATE TABLE without AS SELECT, CREATE
INDEX (including ordinary UNIQUE constraints), and ordinary INSERT/UPDATE/DELETE
with allowed subqueries. SQLite authorizer events admit the operation, not SQL
keyword matching. Engine catalog maintenance is accepted only for admitted
CREATE statements; author catalog DML/query and `__pdb_`/`sqlite_` access are
rejected. ALTER/DROP, views, triggers, virtual/temporary tables, PRAGMA,
attachments, host transaction/savepoint control, maintenance, extension loading,
binds and result-producing SQL are not part of this profile. Unpreparable or
unadmitted SQL returns SQL_REJECTED. Existing trusted schema is also checked
when accessed, including trigger/foreign-key effects. Broader migration DDL
requires a separately tested authorization extension, not a local-full fallback.

At most 64 nonempty statements execute per file; an empty/comment-only migration
is rejected. The safe function allowlist and shared work/cancel/reentry contract
are the same as restricted requests above, with a 100-unit reserve per prepare.
SQL is copied to bounded owned heap, not a large WM6 stack buffer. Migration SQL
and the private version update commit atomically, then the report is published.
Constraint, budget, cancellation, metadata or COMMIT failure preserves the report
and attempts rollback with the existing structured cleanup diagnostics. This is
one-file atomicity, not an all-catalog transaction or a physical durability proof.

```c
PDbRestrictedOptions options;
PDbMigrationReport report;
int rc;

memset(&options, 0, sizeof(options));
options.size = sizeof(options);
options.version = PDB_RESTRICTED_VERSION;
options.vm_budget = PDB_RESTRICTED_VM_MAX;
options.cancelled = read_atomic_cancel;
options.user_data = request;
report.size = sizeof(report);
report.version = PDB_MIGRATION_VERSION;
/* Migration bytes already loaded from an authorized Runtime manifest file. */
rc = PDb_ApplyMigrationRestricted(db, 0, 1, migration_bytes,
    migration_length, &options, &report);
/* Only OK permits proceeding to the next migration/author initialization. */
```

The connection must have no enclosing transaction or outstanding statements and
remain on its owner worker. Callback Close cannot free the active migration;
same-DB status reentry returns STATE, no callback survives return, and no window
pump or cross-thread handle call is permitted. Budgets cover author SQL work,
not a hard wall-clock/VFS deadline; internal fixed-size metadata/transaction
bookkeeping is separate. Deploy the matching DB DLL/import library/header and
its existing JSON dependency; old ABI and local/sync behavior remain unchanged.

## Opt-in persistent local identity

`PDb_BindLocalIdentity(db, identity, bytes)` protects a trusted native storage
owner against mistakenly adopting another database. Identity is explicit-length
UTF-8, 1–2048 bytes without NUL. Only an idle local-full connection with no
statements/outer transaction is accepted. BEGIN IMMEDIATE serializes comparison
and initial binding; an existing private TEXT identity must match every byte.
Different identities, BLOB/corrupt stamps or unclaimed nonempty databases return
SCHEMA_MISMATCH and are not rebound, deleted or silently reset.

First binding requires a pristine file containing only the DB mode metadata
table and its index, with no other metadata keys or schema objects. Existing
browser/application data cannot become a new Runtime namespace by presenting
an unused application ID. The stamp lives in protected `__pdb_meta`, inaccessible
to restricted SQL/migrations; trusted native local-full APIs are unchanged.
This is an exact identity check, not publisher verification, a file-access
sandbox, or a proof against malicious native code/database tampering.

Runtime frames development kind, normalized ID and full provider-canonical root
into this identity. Its finite directory bucket only locates a candidate file;
the full stamp must match before executing migrations. A collision fails closed
instead of sharing data. Filesystem containment, native exclusive lease and
correct EXE-adjacent storage anchoring remain Runtime/provider responsibilities.
Failed calls preserve original sticky/rollback diagnostics; no power-loss
durability guarantee is inferred from logical rollback tests.

## Local and sync modes

The DLL has two modes:

- `PDB_OPEN_LOCAL_FULL_SQL` for local databases that do not participate in
  synchronization.
- `PDB_OPEN_SYNC` for offline-first databases with row-level REST sync state.

Network I/O is intentionally owned by the application host. The host builds a
request with `PDb_SyncBuildRequest()`, sends it through
`positron_http.dll` with HTTPS and its in-memory Bearer token, then passes the
successful response to `PDb_SyncApplyResponse()`.

The sync wire format contains typed row values and versioned operations, never
SQL text. Server-side REST code must provide idempotent operation IDs,
monotonic row versions, tombstones and schema compatibility checks.

Sync mode keeps `__pdb_meta`, `__pdb_sync_tables`, `__pdb_row_state`,
`__pdb_dirty`, `__pdb_outbox` and `__pdb_conflict` private to the DLL. The
dirty queue is durable, so a restart can finish converting a committed local
row change into an outbox operation before the next request is built.

The v1 sync contract supports single-column INTEGER or UTF-8 TEXT primary keys,
NULL/integer/real/text/BLOB values, server-authoritative version conflicts and
bounded bodies compatible with the existing HTTP response limit.

The WinCE port uses SQLite's Win32 VFS and restores its `CreateFileMappingW`
system-call entry for shared file-lock metadata when the pinned upstream table
omits it in the no-WAL/no-mmap build. This anonymous lock mapping is not database
mmap: WAL, database mmap and extension loading remain disabled, and the upstream
amalgamation is unchanged. Journal durability and power-loss recovery still
require separate device validation.

For host-side protocol tests, `scripts/db_sync_fixture.py` provides an independent
in-memory REST fixture with Bearer Token checking, idempotent `op_id` handling,
monotonic versions, pagination and tombstones. It never accepts SQL and is not a
production server.
