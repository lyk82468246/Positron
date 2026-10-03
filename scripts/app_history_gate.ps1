# Consume a complete Debug package deployed by the formal module-audit gate.
# This script never builds, changes the WMDC target, or terminates processes.
param(
    [Parameter(Mandatory=$true)][string] $RemoteRoot,
    [Parameter(Mandatory=$true)][string] $LocalRunRoot,
    [ValidateSet('positron://newtab', 'positron://system')]
    [string] $StartupUrl = 'positron://newtab'
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
$evidenceRoot = Join-Path $runRoot 'app-history'
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
    if ($files.Count -ne 10) { throw 'Expected EXE and nine DLLs in the formal stage.' }
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
    $remoteLog = '\Temp\positron-debug.log'
    $localLog = Join-Path $evidenceRoot 'positron-debug.log'
    [PositronDeviceRapi]::DeleteFileIfExists($remoteLog)
    $appPid = [PositronDeviceRapi]::LaunchProcess(
        ($RemoteRoot + '\positron.exe'), $null, ('--url ' + $StartupUrl))
    $committedUrl = if ($StartupUrl -eq 'positron://system') {
        'positron://about#system'
    } else { 'positron://newtab' }
    $deadline = (Get-Date).AddSeconds(45)
    $complete = $false
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice($remoteLog, $localLog)) {
            $text = Get-Content -LiteralPath $localLog -Raw -Encoding UTF8
            if ($text -match 'selftest FAILED') { throw 'EXE startup selftest failed.' }
            if ($text -match ('debug-session pid={0}\b' -f $appPid) -and
                    $text -match 'history selftest OK' -and
                    $text -match 'fragment-pending selftest OK' -and
                    $text -match 'loading-title selftest OK' -and
                    $text -match 'address-bar selftest OK' -and
                    $text -match 'pointer selftest OK' -and
                    $text -match 'system-info selftest OK' -and
                    $text -match 'internal-pages selftest OK' -and
                    $text -match ('internal-page commit url=' +
                        [regex]::Escape($committedUrl) + ' kind=\d+ history=1 ')) {
                $complete = $true
                break
            }
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    if (!$complete) { throw 'Application pointer/history/fragment-pending selftest or default navigation timed out.' }
    $after = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($entry in $after.GetEnumerator()) {
        if (!$before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
            throw ('New crash dump: ' + $entry.Key)
        }
    }
    Write-Host ('APP HISTORY PASS binary_roundtrip=10/10 crash_check=PASS pid={0}' -f $appPid)
    Write-Host ('Deployment remains at ' + $committedUrl + ': ' + $RemoteRoot)
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
