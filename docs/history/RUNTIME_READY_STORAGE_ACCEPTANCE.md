# READY installed storage acceptance

This record covers native `PRuntime_InstalledStorageOpen` and its reuse of restricted StorageExecuteJson. It does not prove installation UI, persisted EXE registry, final READY COMMIT, installed Browser grants or service queues. Stable calls and trust/lifetime contracts are in [the installed contract](../../positron_runtime/INSTALLED.md#opening-ready-installed-storage).

## Product boundary

The Runtime DLL owns the installed key/stamp, exact manifest schema, committed-record comparison and storage state. A separate v1 require-existing provider retains physical containment/anti-replacement and an existing DB location; the verified installed parent retains root and installation/registry coordination. Runtime calls DB's existing-only opener, never CREATE, BindLocalIdentity or migration. Failure does not publish a storage slot. Providers remain trusted native components, not a malicious-host sandbox.

The shared opaque storage has explicit development-directory or installed-revision ownership, without pointer casts. Native SQL/JSON retains its original statement/row/JSON/VM budgets and COMMIT-before-output behavior. Registration refresh happens around opening and before each valid SQL request; missing/revoked, changed or unreadable registration retires the instance before SQL. An exclusive native installation lease is essential: refresh and application SQL are not one cross-database transaction. Cooperative cancellation does not undo COMMIT or preempt native I/O.

Storage retains its parent until DB/location release. Active callbacks and wrong-thread calls cannot close the storage or parent. Retired info/error remain queryable, but failed storage cannot execute more SQL. Installed ServiceQueueCreate is explicitly UNSUPPORTED with no queue published; existing development queues retain their behavior. No EXE, Core or Browser product wiring changes in this batch.

The combined revision/storage/SQL work plus fixed reserve is compile-time limited to 1280 KiB, excluding SQLite, CSP and native-provider allocations. There is one storage child per installed parent; returned connection and callbacks stay on that creating worker. API inputs are copied where owned and callback targets remain borrowed through Close.

## Regression corpus

TEST1383 uses private native PDb files and modeled committed registration. Fourteen scenarios cover schema 0 and 1 successful write/read, missing DB without creation, wrong identity, unclaimed nonempty DB, future schema, OS-readonly file, partial location token, generation change during open, revocation before open or SQL, generation change before SQL, and cancellation before/after open. Failed/cancelled opens and rejected SQL paths compare exact native file bytes. Success asserts committed changes=1 and typed value 42. Only fixture files/attributes are changed.

The fixture also rejects prepare-kind parents, missing native use grant and future options version before provider calls. Duplicate storage, parent Cancel/Close while retained, real wrong-thread info/close, callback reentry, installed queue creation and repeated close are asserted. Partial tokens and successful leases balance exactly. The migration-count-1 resource fixture is itself prepared/hashed and approved before installed verification; it does not bypass content checks with a hand-filled live handle.

The provider models retained location/registration rather than implementing EXE canonicalization or SQL persistence. Real FULL/I/O/COMMIT/rollback faults, hot-journal/power-loss durability and generic hostile filesystem behavior remain outside this corpus. The earlier DB admission Release INSERT assertion failure remains unclosed in [its own record](DB_EXISTING_ADMISSION_ACCEPTANCE.md); passing this adjacent batch does not explain it or authorize SQL replay.

## Formal and device evidence

Final Debug `tmp/device-runs/20261106-121339-runtime-ready-storage-debug-accepted/` selects `1368-1370,1382,1383,999` and passes 6/6. Final Release `tmp/device-runs/20261106-121504-runtime-ready-storage-release/` selects `1368-1370,1377,1379,1380,1382,1383,999` and passes 9/9. Both contain exactly one TESTBENCH PASS, zero ERROR/FAIL, complete stable logs, correct Core path and no new crash dump. The target remains 240×320/96-DPI WM6 DeviceEmulator; SD Temp, target/internal space and fresh zero-holder/unavailable audits passed. Final deployments were removed only after complete log retrieval, with no device termination/reset, WMDC changes or user DB changes.

The initial Debug `tmp/device-runs/20261106-120717-runtime-ready-storage-debug/` also has 6/6 assertions but is DIAGNOSTIC_ONLY because deployment preservation was requested; it predates the added schema-1/cancel/argument cases and is not the final gate. Its complete-log deployment was later reclaimed by the normal prior-log cleanup policy.

C89, repository/document/boundary audit and whitespace checks pass. Formal builds use only the solution's Debug/Release configurations. PE inspection confirms ARMV4I and 50 exported Runtime public functions; all prior 49 public declarations and existing struct layouts remain unchanged. Consumers must deploy matching Runtime, DB and JSON DLLs: Runtime now imports PDb_OpenExistingLocalUtf8.

The failed Debug build and retry remain in `tmp/runtime-ready-storage-debug-final.txt`, `tmp/runtime-ready-storage-debug-retry.txt` and copied build-failure logs. Application Error 1000 records devenv access violations, including PID 0xE444 at null offset. That retry left an unresponsive owned headless /Build process; only after matching its PID 58436, parent 64604 and exact Positron solution/Debug command was it stopped. No other VS session or device process was terminated, no IDE setting changed, and no failed build staged. The unchanged formal retry then passed.

## Remaining integration

Next compose installed storage with the bounded page queue and Browser grant/revoke lifecycle, preserving owner/copy boundaries and registration retirement before execution. Final PREPARING→READY registration, actual EXE adapter/UI, Archive isolated output and PXI launch remain distinct work. Native READY storage does not suppress development-directory confirmation or make a resource package installable.
