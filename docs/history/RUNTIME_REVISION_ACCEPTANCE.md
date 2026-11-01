# Runtime first-install read-only verification acceptance

This record describes a bounded DLL regression, not PXI installation or EXE authorization. The stable provider, framing and calling contract is [INSTALLED.md](../../positron_runtime/INSTALLED.md).

## Implemented boundary

Runtime owns preparation and installed-verification jobs, full inventory admission, SHA-256 framing, explicit approval snapshot construction and committed native record comparison. Old development/storage ABI is unchanged. Revision 1 is the only supported installed revision; there is no installed SQL opener, registry migration, installer UI, update or author-code launch in this batch.

The regression provider is an immutable in-memory native fixture. Registry callbacks model committed records and exclusive leases; they do not exercise the EXE database. The SHA-256 backend uses device CryptoAPI with CRYPT_VERIFYCONTEXT, trying the default RSA_FULL and RSA_AES provider types for the same algorithm. Each context validates SHA-256("abc") against an independent reference. This ROM supplies the algorithm; other ROMs are not assumed to do so. Missing algorithm is explicitly tested as UNSUPPORTED.

## Independent reference and regression

`scripts/generate_revision_fixture.py` uses Python hashlib and explicit little-endian framing to generate the tracked test header. The five files include raw manifest JSON, entry HTML, empty CSS, a Unicode path with binary bytes and a 131089-byte generated binary resource. This crosses two 64-KiB reads and preserves all bytes. The resource-set digest includes the NUL-terminated domain and the sorted complete inventory.

TEST1377 asserts that reversing enumeration order, moving the canonical root or changing native installation ID leaves the same content identity. It verifies exact installed reads, zero-byte files, confirmation/output canaries and rejection of unsupported revision 2. It rejects same-size tamper, rename/add/remove, missing entry, empty manifest, revoked/missing registration, old/refreshed generation, wrong logical/installation identity, unsupported API/permission/digest, root mismatch and malformed/over-budget inventory. Duplicate installed opens fail BUSY while the first handle retains its leases.

Callbacks attempt reentrant query/Close; a real second thread attempts Step. Per-step read/hash byte and enumeration counters verify the published processing budget. Partial root/registry/hash acquisition, hash update/finish failure, short/extra data, cancellation with an active hash and repeated Close all verify release balance/order and terminal state. These are trusted-provider contract tests, not proof of a generic physical containment adapter, malicious native host resistance or power-loss durability.

## Formal builds and device evidence

Both gates use the formal solution through `scripts/device_gate.ps1`, serially, with selection `1364,1365,1374,1377,999`. Debug evidence is `tmp/device-runs/20261101-194412-runtime-revision-debug-final/`; Release is `tmp/device-runs/20261101-194522-runtime-revision-release-final/`.

Each is 5/5 PASS, with one TESTBENCH PASS, zero ERROR/FAIL, complete stable logs, matched Runtime module path and no new crash dump. The connected WM6 DeviceEmulator is 240×320 at 96 DPI. SD Temp deployment, target-volume/internal-store checks and module-audit `holders=0 unavailable=0` passed. Logs were retrieved before automatic cleanup of these two deployments. No force termination, WMDC change, device reconnect or user database modification was performed.

An earlier Debug `1364,1377,999` pass is retained locally at `tmp/device-runs/20261101-193717-runtime-revision-debug/`; the final gates additionally include the expanded root/lease/empty-manifest checks. It is not substituted for those final results. C89, repository audit and staged whitespace checks passed. Both PE files report ARMV4I, all 38 current public functions are exported, and the 30 pre-existing function declarations are unchanged. Ordinary inherited compiler warnings are not treated as new product failures.

## Registration readback regression

The additive `PRuntime_RevisionReconcileRegistration` owns transition validation and fresh native readback classification. TEST1378 uses the same independently hashed prepared revision, with a trusted in-memory committed-registration fixture; it performs no registry SQL or persistence. Twenty-nine scenarios cover lost commit responses, confirmed/not-committed contradictions, exact old/new records, READY winning an unknown cancellation, explicit FAILED retry confirmation, absent new intents, terminal tombstones, generation overflow/conflict, wrong root/identity/schema/API/capabilities, unreadable records and partial native lease failure. Successful observations are repeated without changing generation or replaying work. Every failure checks output canaries, retained COMPLETE revision, guarded callback reentry and balanced lease cleanup.

Formal Debug `tmp/device-runs/20261101-202326-runtime-registration-debug/` and Release `tmp/device-runs/20261101-202634-runtime-registration-release-retry/` select `1377,1378,999`; each is 3/3 PASS, one TESTBENCH PASS, zero ERROR/FAIL, complete logs and no new crash dump. Both use the same 240×320/96-DPI WM6 target, external SD Temp, dual storage checks and `holders=0 unavailable=0`; deployments were removed only after complete log retrieval. The new DLL preserves previous declarations/layouts and adds one entry. Existing libcss conversion warnings remain; no Runtime compiler warning was introduced.

The first Release attempt exited during VS2008 Runtime build without a compiler diagnostic and staged nothing; its logs remain `tmp/runtime-registration-release.txt` and `tmp/runtime-registration-release-build-failure.log`. No build process remained before the formal retry. It is not recorded as a passing build or device run. Registration readback does not prove real CAS, interrupted file recovery, SQL preparation or installed launch.

## Inactive installation intent regression

The intent coordinator adds owner-bound Begin/Step/GetInfo/Cancel/Close, with one retained prepare parent and one native coordination lease. It proposes only PREPARING or CANCELLED, shares the existing exact readback classifier, and performs at most one native callback per Step. TEST1379 uses a native in-memory conditional-write fixture; its records model persistence but no EXE SQL table or filesystem installation is modified.

The scenarios cover NEW, exact PREPARING resume, explicitly confirmed FAILED retry, wrong/missing/READY/tampered records, generation overflow, acquire/read/partial-lease failure, lost commit response, definite-outcome contradiction and unavailable post-write readback. Cancellation before I/O, after CAS, on PREPARED and during RESUME verifies exact write counts and CANCELLED generation. UNKNOWN close followed by explicit RESUME proves no replay of the first committed write. Duplicate children, parent Cancel/Close/approval/reconciliation guards, real wrong-thread Step/Cancel/Close, child/parent callback reentry, repeated Close and lease balance are asserted. Terminal observations grant no SQL or author-start authority.

Final Debug `tmp/device-runs/20261101-213705-runtime-intent-debug-final/` and Release `tmp/device-runs/20261101-213844-runtime-intent-release-final/` each select `1377-1379,999` and pass 4/4, with one TESTBENCH PASS, zero ERROR/FAIL, complete logs and no new dump. The same 240×320/96-DPI WM6 target, external SD Temp, target/internal space and zero-holder module checks passed; both deployments were removed after complete log retrieval. No force termination, reconnect or user database modification occurred.

The initial Debug VS2008 build exited without a compiler diagnostic and staged nothing; logs are `tmp/runtime-intent-debug.txt` and `tmp/runtime-intent-debug-build-failure.log`. Formal retry `tmp/device-runs/20261101-213546-runtime-intent-debug-retry/` passed before the extra cross-thread/unknown-close-resume assertions; final evidence is the later pair above. These failures/retries do not establish hardware crash or power-loss recovery. C89/audit and whitespace checks passed; both final PE files are ARMV4I with all 44 public functions exported and the previous 39 declarations unchanged. Inherited libcss warnings are separate from Runtime.

## Remaining work

The next implementation is installed data preparation through a live PREPARED intent: independent full stamp/key, complete migration-source admission and non-idempotent suffix recovery. The [recovery contract](../RUNTIME_INSTALL_RECOVERY.md) retains startup blocking on migration failure and separate app/registry commits; READY finalization and require-existing installed storage are not yet delivered. Actual PXI extraction requires its own isolated-output/containment and cleanup contract using Archive. Until these are implemented and tested, the EXE must retain its development confirmation and must not treat a copied snapshot, readback observation or PREPARED intent as a launch grant. Installed updates remain unsupported.
