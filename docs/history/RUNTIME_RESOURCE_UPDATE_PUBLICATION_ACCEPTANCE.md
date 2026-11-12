# Runtime resource publication acceptance

The ARMV4I Debug and Release device runs selected TEST1384–1386 and TEST999.
Both runs passed all four tests, returned complete logs, and produced no new
crash dumps. The first Debug build stopped prematurely after Runtime linked.
The formal retry succeeded. Both deployed DLLs passed the PE machine check and
exposed all 67 declared functions. All 61 prior declarations and 39 prior
public struct definitions were preserved.

- Debug evidence: `tmp/device-runs/20261112-203240-runtime-update-publish-debug`
- Release evidence: `tmp/device-runs/20261112-203500-runtime-update-publish-release`
- Notes binary restoration: `tmp/device-runs/20261106-222650-app-pxi/restored-after-publish-gate/result.txt`

TEST1386 used an in-memory registration provider to simulate unavailable
readback after a committed preparation, resume the same attempt, cancel,
and recover a committed publication after losing the CAS response. It reopened
revision 2 through the installed opener, checked durable-record observation
against the provider, queried the original row, and compared the database
bytes. The old opener rejected revision 2.

The device test verifies the Runtime contract with the fixture provider.
The application's persistent transaction adapter and the real Notes update
remain the next integration gate. Migrations and permission changes remain
outside the resource-only contract.
