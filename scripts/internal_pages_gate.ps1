# Consume a fresh, complete Debug deployment made by debug_capture.bat.
# No build, device selection, or deletion of deployment directories here.
# Every fixture restarts the application; explicit cleanup consent is required.
param(
    [Parameter(Mandatory=$true)][string] $RemoteRoot,
    [Parameter(Mandatory=$true)][string] $LocalRunRoot,
    [switch] $ForceTerminatePositron
)
$ErrorActionPreference = "Stop"
if ([Environment]::Is64BitProcess) { throw "Use internal_pages_gate.bat." }
if (!$ForceTerminatePositron) { throw "Fixture restarts require -ForceTerminatePositron." }
if ($RemoteRoot -notmatch "^(?:\\Storage Card\\Temp\\Positron-device-gate|\\Temp\\Positron-device-gate)\\[A-Za-z0-9._-]+$") {
    throw "Unexpected deployment root."
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$runRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $LocalRunRoot))
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$gateText = Get-Content -LiteralPath (Join-Path $PSScriptRoot "device_gate.ps1") -Raw -Encoding UTF8
$match = [regex]::Match($gateText, "(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$match.Success) { throw "RAPI helper not found." }
Add-Type -TypeDefinition $match.Groups[1].Value -Language CSharp

function Wait-DeviceLog([string] $remote, [string] $local, [string] $pattern)
{
    $deadline = (Get-Date).AddSeconds(30)
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice($remote, $local)) {
            $text = Get-Content -LiteralPath $local -Raw -Encoding UTF8
            if ($text -match $pattern) { return $text }
        }
        Start-Sleep -Milliseconds 150
    } while ((Get-Date) -lt $deadline)
    throw ("Timed out waiting for " + $pattern)
}
function Stop-Application([int] $index)
{
    $remote = $RemoteRoot + "\process-cleanup.log"
    [PositronDeviceRapi]::DeleteFileIfExists($remote)
    [void] [PositronDeviceRapi]::LaunchProcess(
        ($RemoteRoot + "\positron_process_cleanup.exe"), $null, $null)
    $text = Wait-DeviceLog $remote (Join-Path $runRoot ("cleanup-{0}.log" -f $index)) "(?m)^summary "
    if ($text -notmatch "(?m)^summary target_count=\d+ failed=0\b") {
        throw "Application cleanup did not confirm success."
    }
}
$cases = @(
    @("", "positron://newtab", 7),
    @("--url positron://about", "positron://about", 9),
    @("--url POSITRON://NEWTAB/", "positron://newtab", 7),
    @("--url positron://history", "positron://history", 7),
    @("--url positron://downloads/", "positron://downloads", 7),
    @("--url positron://settings", "positron://settings", 7),
    @("--url positron://version/", "positron://about#version", 9),
    @("--url POSITRON://SYSTEM", "positron://about#system", 9),
    @("--url positron://about/#SYSTEM", "positron://about#system", 9),
    @("--url welcome", "positron://welcome", 3),
    @("--url controls", "positron://controls", 3),
    @("--url positron://controls?q=test", "positron://controls?q=test", 3),
    @("--url positron://unknown", "positron://newtab", 7),
    @("--url positron://quit", "positron://newtab", 7),
    @("", "positron://newtab", 7)
)
$connected = $false
try {
    [PositronDeviceRapi]::Connect()
    $connected = $true
    $before = [PositronDeviceRapi]::SnapshotCrashDumps()
    $remoteLog = "\Temp\positron-debug.log"
    $index = 0
    foreach ($case in $cases) {
        Stop-Application $index
        [PositronDeviceRapi]::DeleteFileIfExists($remoteLog)
        $appPid = [PositronDeviceRapi]::LaunchProcess(
            ($RemoteRoot + "\positron.exe"), $null, $case[0])
        $localLog = Join-Path $runRoot ("page-{0}.log" -f $index)
        $text = Wait-DeviceLog $remoteLog $localLog ("pid={0} .*internal-page commit" -f $appPid)
        $expected = [regex]::Escape($case[1])
        if ($text -notmatch ("debug-session pid={0}\b" -f $appPid) -or
                $text -notmatch "internal-pages selftest OK" -or
                $text -match "selftest FAILED" -or
                $text -notmatch ("internal-page commit url={0} kind=\d+ history=1 focus={1} scroll=\d+,\d+ script=0" -f $expected,$case[2])) {
            throw ("Unexpected navigation result: " + $case[0])
        }
        if ($case[1] -match "#system$" -and
                $text -notmatch "scroll=\d+,[1-9]\d* script=0") {
            throw "System alias did not reveal its section."
        }
        Write-Host ("[internal-pages] OK {0}: {1} pid={2}" -f $index,$case[1],$appPid)
        $index++
    }
    $after = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($entry in $after.GetEnumerator()) {
        if (!$before.ContainsKey($entry.Key) -or $before[$entry.Key] -ne $entry.Value) {
            throw ("New crash dump: " + $entry.Key)
        }
    }
    Write-Host ("INTERNAL PAGES PASS cases={0} crash_check=PASS; default newtab remains open." -f $index)
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
