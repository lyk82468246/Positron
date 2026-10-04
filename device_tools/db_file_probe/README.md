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

This first slice does not prove concurrent-process locks, hot-journal recovery,
I/O faults, actual volume exhaustion or physical power-loss durability.

`python scripts/test_db_file_probe.py` checks fixture SQL, UTF-8 bytes, ownership
guards, process sequencing and project boundaries offline. Its host SQLite
close/reopen is only an oracle check, not WM6 or new-process acceptance.
`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/test_db_file_gate.ps1`
runs the real log validators on synthetic text without loading RAPI or executing
the deployment body. Neither offline check replaces Debug/Release device gates.
