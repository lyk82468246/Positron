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

## Remaining work

The next design boundary is installed storage and first-install registration recovery: independent full data identity, migration failure that blocks startup, and explicit reconciliation of the app DB and registry DB's separate commits. Actual PXI extraction requires its own isolated-output/containment and cleanup contract using Archive. Until these are implemented and tested, the EXE must retain its development confirmation and must not treat a copied snapshot as a persisted grant. Installed updates remain unsupported.
