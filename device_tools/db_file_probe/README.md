# DB file fixture probe

This internal WM6/ARMV4I executable consumes only the public `positron_db.dll`
API. It is not shipped in the application CAB and never compiles SQLite or DB
implementation sources. Build through the formal solution; a standalone
compiler invocation is not an accepted build.

`--run-unicode` creates a new, exclusive `fixture-PID-tick` directory beneath
its own `db-file-*` gate package. The executable and DLL package must be on the
explicit SD gate root. `--run-unicode-internal` keeps that same binary package,
but creates an independent fixture beneath the dedicated internal Temp
`Positron-device-gate/db-file-fixtures` root. Results identify the actual file
path so SD sharing and the internal filesystem are not conflated.
The Chinese directory and database filename are
passed to `PDb_OpenUtf8Ex` as UTF-8. Existing directories, files and logs are
never overwritten; the creator reserves an empty database with `CREATE_NEW`
before opening it through the public DB API. No fixture file or directory is
deleted by the executable.

The coordinator never opens a DB. Three independent processes execute in order:

1. `create` migrates the fixture schema, binds and commits Chinese TEXT/binary
   BLOB values, rolls back another row, checks a failed migration, and performs
   a normal close/reopen before closing and exiting.
2. After the creator has exited with code zero, `read` cold-opens the same file,
   verifies exact bytes/schema/integrity and commits another Chinese row.
3. After the reader has exited, `verify-b` cold-opens and verifies both commits.

Each process logs its PID, executable and actual DB DLL path. Exited child
process handles are retained until the whole sequence ends, avoiding PID reuse
in the identity evidence. Schema version is
checked through the public idempotent migration contract, not SQL on private
metadata. Statements and DB handles are released before a successful exit.
The file is capped at 512 4096-byte pages; this cap is a fixture safety budget,
not a file-system FULL test. Each child has a 60-second deadline. Timeout fails
the gate; termination can only use the still-owned process handle returned by
this coordinator's `CreateProcess`, never an enumerated or caller-supplied PID.

`--run-locks` and `--run-locks-internal` use the same owned paths and safety
budgets. A creator exits before two concurrent pairs run, followed by an
independent cold verifier (six processes total). Each pair opens both handles
before acquiring locks. Flushed, exclusive marker files coordinate the phases,
with a 15-second peer deadline; a sleep alone never establishes lock evidence.
The read/write pair holds a real read transaction, requires native COMMIT BUSY
with the writer still active, checks the old snapshot, then releases the reader
and retries the commit before checking a new snapshot. The write/write pair
requires a second `BEGIN IMMEDIATE` to return native BUSY without creating a
transaction, then retries after the first writer commits. The cold verifier
checks all four rows, schema version and integrity after both pairs have exited.
Failures are reaped before the coordinator reports completion.

`--run-journal` / `--run-journal-internal` run five independent processes:
creator, seed, interrupted writer, recoverer and cold verifier. The seed commits
64 exact 2048-byte BLOB rows under schema version 2. A four-page cache and enabled
cache spill force the writer's uncommitted changes out of memory. After its
flushed ready marker (active write transaction, zero statements, no commit,
rollback or close), only its still-owned CreateProcess handle is terminated,
with a dedicated exit code. Failure to observe readiness/termination fails.
The writer never returns normally as a successful child.

Before launching recovery, the coordinator validates the real journal magic,
record/page/sector bounds and preserves CREATE_NEW `before.sqlite`, `hot.sqlite`
and `hot.sqlite-journal` copies. It never edits the original DB or journal.
The journal fixture caps the DB at 128 pages (512 KiB), each journal copy at
1 MiB and retains the 5 MiB storage preflight. A fresh public-API consumer must
recover the exact committed TEXT/BLOB/schema, pass integrity and commit a new
row; another new process verifies that commit. `-Suite Journal` additionally
retrieves both raw snapshots and requires different baseline/hot DB hashes,
so a nonempty journal without an actual database spill is insufficient.

`--run-quota` / `--run-quota-internal` use four sequential processes: creator,
quota writer, cold reopener and final verifier. Each quota consumer verifies a
32-page (128 KiB) limit and FULL synchronous mode. The writer changes existing
TEXT and inserts a small BLOB inside a transaction, then a prepared 256 KiB
zeroblob insert must fail with native FULL. It captures the root error and live
automatic rollback state before finalizing the failed statement; no explicit
rollback hides the state. Both earlier changes must be absent. A new process
verifies exact original bytes/schema/integrity and commits small rows; a final
new process checks that commit. Lower migration version rejection precedes the
empty current-version check, so version verification cannot repair lost state.
`-Suite Quota` checks this sequence, error/finalize/rollback markers and the
retrieved database size. This is a file page quota test, not volume exhaustion,
injected I/O/COMMIT/rollback failure, or physical power-loss acceptance. The
5 MiB free-space preflight remains unchanged. A separate CREATE_NEW 4096-byte
scratch file diagnoses native `SetEndOfFile` truncation to 2048 bytes and logs
the result and Win32 error. It never repairs or truncates the database/journal;
diagnostic success cannot replace the SQLite rollback assertions. Quota runs
internal storage before SD to retain independent evidence when SD fails, but
both storage variants must pass for acceptance.

Logs and files remain for the host gate to retrieve and hash before scoped
cleanup. A complete package, free-space preflight, guest no-foreign-DLL audit,
round-trip binary identity and crash inventory are host-gate requirements;
launching this executable alone is not device acceptance.

`scripts/db_file_gate.ps1` consumes a fresh preserved `ModuleAuditOnly` formal
stage in a dedicated `db-file-*` SD root, using 32-bit Windows PowerShell and an
explicitly coordinated exclusive device window. It refuses an unregistered
project or a partial/mismatched package. It runs the SD and internal data
variants, validates process order and exact module/file paths, retrieves stable
logs and database hashes, repeats the guest DLL-holder audit and checks crash
inventory. It never selects/resets a device or kills a PID. Only fully verified
fixture directories and the exact package may be cleaned after evidence is
retrieved; failure preserves them. `-PreserveDeployment` produces diagnostic
evidence, not a final acceptance result.

Pass `-Suite Locks` to run the concurrent suite; the default `Unicode` preserves
the original three-process sequence. The gate verifies overlapping process
pairs, native BUSY/error/transaction state, snapshot visibility, retry and cold
reopen rather than accepting a generic PASS line alone.
Unicode/Locks do not prove hot-journal recovery. Journal tests controlled process
termination, not physical power loss; no suite proves injected I/O faults or
actual volume exhaustion. A fixture implementation is not acceptance without
complete device evidence.

`python scripts/test_db_file_probe.py` checks fixture SQL, UTF-8 bytes, ownership
guards, process sequencing and project boundaries offline. Its host SQLite
close/reopen is only an oracle check, not WM6 or new-process acceptance.
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_db_file_gate.ps1`
runs the real log validators on synthetic text without loading RAPI or executing
the deployment body. Neither offline check replaces Debug/Release device gates.
