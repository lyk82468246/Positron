# Existing local DB read-only admission acceptance

This record covers `PDb_VerifyExistingLocalUtf8`, not a writable DB opener, Runtime READY registration or application launch. Stable calls and limits are in the [DB README](../../positron_db/README.md#verifying-an-existing-local-database-without-writes).

## Implementation and failure boundary

The DLL opens a private SQLite connection with READONLY/FULLMUTEX and no CREATE, and never invokes metadata initialization or identity binding. One read transaction checks a real generated metadata table, canonical TEXT local mode, exact TEXT identity and strict expected schema; BLOB checks precede text coercion. It closes before returning, publishing no handle or authority. Fixed connection/query/VM limits are inspection guards, not native-I/O or whole-SQLite memory deadlines. The 16-MiB page guard measures logical pages, not trailing physical bytes.

Missing/inaccessible paths fail without creating them. Empty/foreign/malformed or mismatched metadata fails without adopting, initializing, migrating or deleting data. The progress interrupt is disabled before failure cleanup, and exhaustion is categorized as LIMIT rather than cancellation. A successful result remains a point-in-time observation; callers must not use a later old CREATE-capable open as a replacement for verified existing-handle admission.

## Regression

TEST1381 creates a fresh exclusive private fixture directory in the deployment. Seventeen file cases cover missing/zero-byte/non-SQLite files, valid schema 0/1, wrong/missing identities, nonempty unclaimed data, future/malformed schema, BLOB/NUL identities, noncanonical/BLOB mode, a metadata view, malformed table structure and an OS-readonly file. Each is inspected twice. Existing bytes are copied through native read-only file handles before and after, compared by exact length/memcmp, and journal absence is asserted. The missing file remains absent. Successful fixtures and root are deleted only after the checks; failures would retain their isolated evidence.

Error-output unknown version preserves its canary. URI/memory spelling, over-limit path length and future options version fail before file admission. The fixture uses only public DB APIs for setup and native file inspection; no SQLite implementation is compiled into the host. VM exhaustion, extreme schema resources, logical-page maximum and actual hot-journal recovery failures are not claimed as exercised. Physical FULL/I/O/power-loss work remains under its existing user-directed suspension.

## Formal build and WM6 evidence

Final Release evidence is `tmp/device-runs/20261102-133158-db-existing-release-accepted/`; final Debug is `tmp/device-runs/20261102-133512-db-existing-debug-accepted/`. Both select `1366,1368,1369,1380,1381,999` and pass 6/6, with one TESTBENCH PASS, zero ERROR/FAIL, complete stable logs and no new dump. The direct restricted-migration/development-storage/SQL-JSON/installed-data neighbors remain passing. An earlier Debug pass `tmp/device-runs/20261102-132747-db-existing-debug-retry/` predates the final budget-diagnostic/cleanup refinement and is not substituted for final evidence.

The target is the current 240×320/96-DPI WM6 DeviceEmulator. External SD Temp, target/internal space and module `holders=0 unavailable=0` checks passed. Both final deployments were cleaned after complete log retrieval. No force termination, device reset, WMDC change or user database modification occurred. Test-only file attributes were restored before deletion. C89, repository/document audit and staged whitespace checks passed; both final DB DLLs are ARMV4I with all 43 public functions exported and the previous 42 declarations preserved.

Incomplete build logs remain at `tmp/db-existing-debug.txt`, `tmp/db-existing-debug-final.txt`, `tmp/db-existing-release.txt`, `tmp/db-existing-release-final.txt` and their corresponding build-failure logs. The first Release attempt hit CAB source-file lookup while the DB DLL was being rebuilt; other premature exits had no compiler diagnostic. Host Application Error evidence in `tmp/db-existing-build-events.txt` records devenv access violations, including msenv.dll at offset 0x0020479d. No failed build staged/deployed or counts as PASS. Formal retries used the unchanged build/stage entry and solution, without IDE-setting changes, process kills or toolchain bypass.

## Remaining prerequisite

Writable existing-handle admission must eliminate the probe-to-use replacement window, reject creation/metadata initialization/first identity claiming and retain normal failure/locking semantics. Runtime must then compose that handle with native containment/coordination, READY storage/service lifecycle and final registration. This read-only verifier alone does not remove development warnings, grant author SQL or make PXI installation complete.
