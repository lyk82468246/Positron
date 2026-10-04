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
