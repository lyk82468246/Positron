# Runtime same-package restoration acceptance

The WM6 ARMV4I Debug and Release gates each passed TEST1384–1386 and TEST999.
Both recovered complete logs and reported no new crash dumps.

- Debug: `tmp/device-runs/20261113-234437-runtime-restore-debug-final`
- Release: `tmp/device-runs/20261114-083022-runtime-restore-release`

TEST1386 restores revision 2 through a native fixture provider, simulates a
committed CAS with unavailable readback, observes the committed READY record,
rejects a second write with the old tombstone, and verifies that the normal
installed opener admits the restored revision. The pre-existing database
is compared byte-for-byte. The first Debug run exposed a fixture dependency
on an attempt row that restoration correctly clears. The final fixture retains
an independent snapshot for the subsequent revision 1 revocation check.

The app's persistent provider, native confirmation, and a real Notes recovery
remain the application integration gate.
