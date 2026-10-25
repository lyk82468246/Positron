# Consume a complete Debug package deployed by the formal module-audit gate.
# This script never builds, changes the WMDC target, or terminates processes.
param(
    [Parameter(Mandatory=$true)][string] $RemoteRoot,
    [Parameter(Mandatory=$true)][string] $LocalRunRoot,
    [ValidateSet('positron://newtab', 'positron://system', 'positron://history')]
    [string] $StartupUrl = 'positron://newtab',
    [switch] $NormalStartup,
    [switch] $VisitNavigation,
    [string] $FixtureDatabase = '',
    [switch] $ReuseFixture,
    [switch] $ManualFixtureReview,
    [switch] $ScrollPaintOnly,
    [ValidatePattern('^app-history(?:-[a-z0-9]+)*$')]
    [string] $AttemptName = 'app-history'
)
$ErrorActionPreference = 'Stop'
function Get-Sha256([string] $path)
{
    $stream = [IO.File]::OpenRead($path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}
if ([Environment]::Is64BitProcess) { throw 'Use app_history_gate.bat.' }
if ($RemoteRoot -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\[A-Za-z0-9._-]+$') {
    throw 'Expected an explicit SD deployment under Positron-device-gate.'
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $LocalRunRoot))
$stageRoot = Join-Path $runRoot 'stage'
$fixture = $null
if ($ScrollPaintOnly -and ($FixtureDatabase -or $NormalStartup -or $VisitNavigation -or
        $ManualFixtureReview -or $StartupUrl -ne 'positron://newtab')) {
    throw 'Scroll-only runs an isolated, non-navigating Debug fixture.'
}
if ($ReuseFixture -and !$FixtureDatabase) { throw 'Reuse requires an exact fixture manifest.' }
if ($ManualFixtureReview -and !$FixtureDatabase) { throw 'Manual review requires an isolated fixture.' }
if ($FixtureDatabase) {
    $FixtureDatabase = [IO.Path]::GetFullPath($FixtureDatabase)
    if ($NormalStartup -or $VisitNavigation -or $StartupUrl -ne 'positron://history' -or
            $FixtureDatabase -ne (Join-Path $runRoot 'history-fixture\history-fixture.db')) {
        throw 'Require an isolated run-local history fixture and explicit history startup.'
    }
    $fixture = Get-Content ([IO.Path]::ChangeExtension($FixtureDatabase, '.json')) -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($fixture.total -ne 80 -or (Get-Sha256 $FixtureDatabase) -ne $fixture.database_sha256) {
        throw 'History fixture count or hash mismatch.'
    }
}
$auditPath = Join-Path $runRoot 'module-audit-result.txt'
$audit = Get-Content -LiteralPath $auditPath -Raw -Encoding UTF8
if ($audit -notmatch '(?m)^status=PASS\s*$' -or
        $audit -notmatch '(?m)^remote_root=' + [regex]::Escape($RemoteRoot) + '\s*$' -or
        $audit -notmatch 'holders=0 unavailable=0') {
    throw 'The matching formal deployment must have a successful module audit.'
}
$gate = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'device_gate.ps1') -Raw -Encoding UTF8
$source = [regex]::Match($gate, "(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$source.Success) { throw 'RAPI helper not found.' }
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
$evidenceRoot = Join-Path $runRoot $AttemptName
if (Test-Path $evidenceRoot) { throw 'Never overwrite an earlier acceptance attempt.' }
$roundtripRoot = Join-Path $evidenceRoot 'roundtrip'
New-Item -ItemType Directory -Path $roundtripRoot -Force | Out-Null
$connected = $false
try {
    [PositronDeviceRapi]::Connect()
    $connected = $true
    $before = [PositronDeviceRapi]::SnapshotCrashDumps()
    # Recheck current holders rather than trusting an earlier audit snapshot.
    $remoteAudit = $RemoteRoot + '\module-audit.log'
    [PositronDeviceRapi]::DeleteFileIfExists($remoteAudit)
    [void] [PositronDeviceRapi]::LaunchProcess(
        ($RemoteRoot + '\positron_process_cleanup.exe'), $null, '--audit-modules')
    $localAudit = Join-Path $evidenceRoot 'module-audit.log'
    $deadline = (Get-Date).AddSeconds(30)
    $auditComplete = $false
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice($remoteAudit, $localAudit)) {
            $text = Get-Content -LiteralPath $localAudit -Raw -Encoding UTF8
            if ($text -match '(?m)^module_audit holders=(\d+) unavailable=(\d+)\s*$') {
                if ($Matches[1] -ne '0' -or $Matches[2] -ne '0') {
                    throw 'DLL holders or an unavailable snapshot prevent application launch.'
                }
                $auditComplete = $true
                break
            }
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    if (!$auditComplete) { throw 'Current device module audit timed out.' }
    $files = @(Get-ChildItem -LiteralPath $stageRoot -File |
        Where-Object { $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    if ($files.Count -ne 11) { throw 'Expected EXE and ten DLLs in the formal stage.' }
    foreach ($file in $files) {
        $local = Join-Path $roundtripRoot $file.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(
                ($RemoteRoot + '\' + $file.Name), $local)) {
            throw ('Cannot read back ' + $file.Name)
        }
        $expected = Get-Sha256 $file.FullName
        if ((Get-Sha256 $local) -ne $expected) {
            throw ('Mixed deployment: ' + $file.Name)
        }
    }
    if ($null -ne $fixture) {
        # Copy uses a unique temporary file followed by CeMoveFile, which
        # refuses an existing destination. Never delete/replace a user DB.
        if (!$ReuseFixture) {
            [PositronDeviceRapi]::CopyFileToDevice($FixtureDatabase, ($RemoteRoot + '\positron.db'))
        }
        $copied = Join-Path $evidenceRoot 'fixture-roundtrip.db'
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(($RemoteRoot + '\positron.db'), $copied) -or
                (Get-Sha256 $copied) -ne $fixture.database_sha256) {
            throw 'History fixture roundtrip mismatch.'
        }
    }
    $remoteLog = '\Temp\positron-debug.log'
    $localLog = Join-Path $evidenceRoot 'positron-debug.log'
    [void][PositronDeviceRapi]::TryCopyFileFromDevice($remoteLog,
            (Join-Path $evidenceRoot 'previous-debug.log'))
    [PositronDeviceRapi]::DeleteFileIfExists($remoteLog)
    if ($NormalStartup -and $StartupUrl -ne 'positron://newtab') {
        throw 'NormalStartup uses no command-line URL; require default newtab settings.'
    }
    $arguments = if ($NormalStartup) { $null } else { '--selftest-ui --url ' + $StartupUrl }
    if ($ManualFixtureReview) { $arguments = '--url positron://history' }
    if ($ScrollPaintOnly) { $arguments = '--selftest-scroll-paint' }
    if ($VisitNavigation) {
        if ($NormalStartup -or $StartupUrl -ne 'positron://newtab') {
            throw 'VisitNavigation uses the full UI suite and its fixed HTTPS target.'
        }
        $arguments = '--selftest-ui --url https://www.iana.org/help/example-domains --eval "setTimeout(function(){location.href=\u0027positron://history\u0027;},1000)"'.Replace('\u0027', "'")
    }
    $appPid = [PositronDeviceRapi]::LaunchProcess(
        ($RemoteRoot + '\positron.exe'), $null, $arguments)
    $committedUrl = if ($VisitNavigation) {
        'positron://history'
    } elseif ($StartupUrl -eq 'positron://system') {
        'positron://about#system'
    } else { $StartupUrl }
    $expectedHistory = if ($VisitNavigation) { 2 } else { 1 }
    $deadline = (Get-Date).AddSeconds($(if ($VisitNavigation) { 180 } else { 90 }))
    $complete = $false
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice($remoteLog, $localLog)) {
            $text = Get-Content -LiteralPath $localLog -Raw -Encoding UTF8
            if ($text -match 'selftest FAILED') { throw 'EXE startup selftest failed.' }
            if ($ScrollPaintOnly) {
                if ($text -match ('debug-session pid={0}\b' -f $appPid) -and
                        $text -match 'scroll-paint selftest OK dpi=96,192 moves=9 pixels=exact') {
                    $complete = $true
                    break
                }
                Start-Sleep -Milliseconds 150
                continue
            }
            if ($NormalStartup -and $text -match 'selftest|https://example\.com/') {
                throw 'Normal startup ran a UI fixture.'
            }
            $suiteComplete = $NormalStartup -or $ManualFixtureReview -or (
                    $text -match 'history selftest OK' -and
                    $text -match 'fragment-pending selftest OK' -and
                    $text -match 'script-scheduling selftest OK' -and
                    $text -match 'tabs selftest OK' -and
                    $text -match 'preferences-policy selftest OK candidate=fixed enabled=executed disabled=no_session_or_fetch' -and
                    $text -match 'loading-title selftest OK' -and
                    $text -match 'address-bar selftest OK' -and
                    $text -match 'pointer selftest OK' -and
                    $text -match 'scroll-paint selftest OK dpi=96,192 moves=9 pixels=exact' -and
                    $text -match 'history-wrap selftest OK dpi=96,128,192 widths=240,480,240 text=exact href=exact' -and
                    $text -match 'system-info selftest OK' -and
                    $text -match 'internal-pages selftest OK')
            $visitComplete = !$VisitNavigation -or (
                    $text -match 'settings-result request=\d+ op=3 result=0' -and
                    $text -match 'settings-result request=\d+ op=4 result=0' -and
                    $text -match 'internal-page commit url=positron://history kind=5 history=2 focus=13')
            if ($null -ne $fixture) {
                $visitComplete = $text -match 'settings-result request=\d+ op=4 result=0' -and
                    $text -match 'internal-page commit url=positron://history kind=5 history=1 focus=44'
            }
            if ($VisitNavigation -and $visitComplete) {
                $layouts = [regex]::Matches($text,
                    'relayout pass=\d+ network=0 .*size=(\d+)x\d+ extent=(\d+)x\d+')
                if (!$layouts.Count) { throw 'Actual history layout evidence is missing.' }
                $lastLayout = $layouts[$layouts.Count - 1]
                if ([int]$lastLayout.Groups[2].Value -gt [int]$lastLayout.Groups[1].Value) {
                    throw 'Actual history page still exceeds its viewport width.'
                }
            }
            if ($visitComplete -and $text -match ('debug-session pid={0}\b' -f $appPid) -and
                    $suiteComplete -and
                    $text -match 'startup-window visible=1 foreground=1 page_visible=1' -and
                    $text -match ('internal-page commit url=' +
                        [regex]::Escape($committedUrl) + ' kind=\d+ history=' + $expectedHistory + ' ')) {
                $complete = $true
                break
            }
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    if (!$complete) { throw 'Application selftest, visible foreground window or default navigation timed out.' }
    $after = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($entry in $after.GetEnumerator()) {
        if (!$before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
            throw ('New crash dump: ' + $entry.Key)
        }
    }
    if ($ScrollPaintOnly) {
        # A terminal log alone is insufficient: require normal fixture exit.
        [PositronDeviceRapi]::DeleteFileIfExists($remoteAudit)
        [void][PositronDeviceRapi]::LaunchProcess(
            ($RemoteRoot + '\positron_process_cleanup.exe'), $null, '--audit-modules')
        $exitAudit = Join-Path $evidenceRoot 'exit-module-audit.log'
        $deadline = (Get-Date).AddSeconds(30)
        $exited = $false
        do {
            if ([PositronDeviceRapi]::TryCopyFileFromDevice($remoteAudit, $exitAudit)) {
                $exitText = Get-Content $exitAudit -Raw -Encoding UTF8
                if ($exitText -match '(?m)^module_audit holders=0 unavailable=0\s*$') {
                    $exited = $true
                    break
                }
                if ($exitText -match 'module_audit holders=') {
                    # The pixel marker precedes normal Core/font teardown.
                    # Refresh snapshots within the same fixed exit deadline.
                    Start-Sleep -Milliseconds 300
                    [PositronDeviceRapi]::DeleteFileIfExists($remoteAudit)
                    [void][PositronDeviceRapi]::LaunchProcess(
                        ($RemoteRoot + '\positron_process_cleanup.exe'), $null, '--audit-modules')
                }
            }
            Start-Sleep -Milliseconds 150
        } while ((Get-Date) -lt $deadline)
        if (!$exited) { throw 'Scroll fixture exit audit timed out.' }
        $after = [PositronDeviceRapi]::SnapshotCrashDumps()
        foreach ($entry in $after.GetEnumerator()) {
            if (!$before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
                throw ('New crash dump after scroll teardown: ' + $entry.Key)
            }
        }
        [IO.File]::WriteAllLines((Join-Path $evidenceRoot 'scroll-result.txt'), @(
            'status=PASS', 'dpi=96,192', 'moves=9', 'pixels=exact',
            'binary_roundtrip=11/11', 'exit_audit=holders=0 unavailable=0',
            'crash_check=PASS', ('pid=' + $appPid), 'full_ui_selftests=NOT_RUN'))
        Write-Host ('APP SCROLL PASS dpi=96,192 moves=9 pixels=exact binary_roundtrip=11/11 exit_audit=zero crash_check=PASS pid={0}' -f $appPid)
        return
    }
    if ($ManualFixtureReview) {
        Write-Host ('APP HISTORY MANUAL READY pid={0}; full UI selftests NOT RUN, not an acceptance PASS' -f $appPid)
    } else {
        Write-Host ('APP HISTORY PASS binary_roundtrip=11/11 crash_check=PASS pid={0}' -f $appPid)
    }
    if ($null -ne $fixture) {
        [IO.File]::WriteAllLines((Join-Path $evidenceRoot 'fixture-result.txt'), @(
            $(if ($ManualFixtureReview) { 'status=READY' } else { 'status=PASS' }),
            'fixture_rows=80', 'fixture_roundtrip=exact', 'binary_roundtrip=11/11',
            $(if ($ManualFixtureReview) { 'ui_selftests=NOT_RUN' } else { 'ui_selftests=PASS' }),
            'loaded_page=16_with_older',
            ('pid=' + $appPid), 'crash_check=PASS', 'manual_review=pending'))
    }
    Write-Host ('Deployment remains at ' + $committedUrl + ': ' + $RemoteRoot)
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
