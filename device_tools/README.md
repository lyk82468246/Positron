# Device process cleanup helper

`positron_process_cleanup.exe` is an internal Windows Mobile 6 Professional
device-gate helper. It has no Positron DLL imports, enumerates the device
process list, and terminates only exact `positron.exe` matches or stale gate
host names beginning with `test_host-run-` when the gate is run with
`-ForceTerminatePositron`.

The helper is not a public runtime component and is not launched by
`positron.exe` or by normal `test_host.exe` runs. The device gate copies it to
the unique deployment directory, launches it before `test_host.exe`, retrieves
`process-cleanup.log`, and fails closed if a matching process cannot be
terminated. With no matching process it records `target_count=0` and succeeds.

Build it through the formal solution configuration; do not compile this file
as a desktop executable. The helper is ARMV4I-only in the current WM6
Professional baseline.
