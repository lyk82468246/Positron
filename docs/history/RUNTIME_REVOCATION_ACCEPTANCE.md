# Runtime revocation acceptance

The WM6 ARMV4I Debug and Release gates each passed TEST1384–1386 and TEST999.
Both retrieved complete logs and reported no new crash dumps. The staged PE
files exposed all 69 declared Runtime functions, and comparison against
cef74095 preserved all 67 previous declarations and 43 public structures.

- Debug: `tmp/device-runs/20261113-230639-runtime-revoke-debug-final`
- Release: `tmp/device-runs/20261113-231037-runtime-revoke-release`

TEST1386 exercises revision 1 and 2 revocation with a native fixture provider,
active lease and PREPARING rejection, cancellation, confirmed no-write,
lost CAS response with and without immediate readback, and read-only recovery.
Both installed openers reject the inactive record. The original private
database is compared byte-for-byte after revocation.

The first external deployment hit SQLite journal CANTOPEN/native2 in TEST1385.
A complete internal deployment exceeded free space. The successful runs staged
the package on the card and created per-process SQLite fixtures in internal
Temp. The external mapped-volume issue remains open. The app's persistent
provider and removal UI need a separate Notes integration gate.
