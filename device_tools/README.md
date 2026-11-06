# Device process cleanup helper

`positron_process_cleanup.exe` is an internal Windows Mobile 6 Professional
device-gate helper. It has no Positron DLL imports, enumerates the device
process list, and terminates only exact `positron.exe` matches or stale gate
host names beginning with `test_host-run-` when the gate is run with
`-ForceTerminatePositron`.

The helper is not a public runtime component and is not launched by
`positron.exe` or by normal `test_host.exe` runs. The device gate copies it to
the unique deployment directory. With `-ForceTerminatePositron` it first
launches the cleanup mode, retrieves `process-cleanup.log`, and fails closed if
a matching process cannot be terminated. With no matching process it records
`target_count=0` and succeeds.

The gate then launches the same helper with `--audit-modules`. This read-only
mode enumerates every device process and its Toolhelp module list, matching all
staged Positron DLL basenames. It writes `module-audit.log` and only succeeds
when the final line is `module_audit holders=0 unavailable=0`. Any matching
holder or incomplete module snapshot fails closed before `test_host.exe` is
started. The audit helper itself has no Positron DLL imports, so the result is a
guest-side module reference check rather than a desktop simulator-process check.

Build it through the formal solution configuration; do not compile this file
as a desktop executable. The helper is ARMV4I-only in the current WM6
Professional baseline.

## Explicit CAB observation

`--probe-cab` is a separate diagnostic mode. Stage an explicitly approved
`install-probe.cab` beside the helper, then invoke this exact switch. It delegates
to the native Wceload executable with no destination prompt, no UI, and no CAB
removal. It never changes device security policy or terminates the installer.
Before invocation, the caller must verify the CAB hash, available installation
space, and zero references to the DLLs being replaced.

`cab-probe.log` records the loader PID and its real exit code after a bounded
30-second wait. An unresolved wait retains the loader; do not restart it solely
because observation timed out. Helper exit zero means observation completed,
not installation success. Independently read back destination files and registry
values before accepting an install. Missing input/log creation refuses to launch.
Unknown nonempty switches return an error without invoking cleanup.
