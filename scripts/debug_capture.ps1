<################################################################################
  Debug-only positron.exe capture helper.

  Full run (stage, deploy, launch, and take an initial log snapshot):
    scripts\debug_capture.bat -Configuration Debug

  Keep pulling the log while the user exercises the running application:
    scripts\debug_capture.bat -PullOnly `
        -RemoteRoot "\Storage Card\Temp\Positron-device-gate\debug-capture-..." `
        -FollowSeconds 300

  The WMDC device must already be connected.  This helper never selects,
  cradles or resets a device.  Pass -ForceTerminatePositron when a stale
  positron.exe must be removed before the captured process is launched.
################################################################################>

param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug",
    [string] $RemoteRoot = "",
    [string] $LocalRunRoot = "",
    [switch] $PullOnly,
    [int] $FollowSeconds = 0,
    [switch] $ForceTerminatePositron
)

$ErrorActionPreference = "Stop"

function Write-Capture([string] $message)
{
    Write-Host ("[debug-capture] " + $message)
}

function Get-RunRoot([string] $requested, [string] $stamp)
{
    if ([string]::IsNullOrEmpty($requested)) {
        return Join-Path $repoRoot ("tmp\device-runs\debug-capture-" + $stamp)
    }
    if ([IO.Path]::IsPathRooted($requested)) {
        return [IO.Path]::GetFullPath($requested).TrimEnd("\")
    }
    return [IO.Path]::GetFullPath((Join-Path $repoRoot $requested)).TrimEnd("\")
}

function Get-RelativePath([string] $root, [string] $path)
{
    $rootUri = New-Object Uri(($root.TrimEnd("\") + "\"))
    $pathUri = New-Object Uri($path)
    return [Uri]::UnescapeDataString(
            $rootUri.MakeRelativeUri($pathUri).ToString()).Replace("/", "\")
}

function Ensure-RemoteDirectoryTree([string] $path)
{
    $current = ""
    foreach ($segment in ($path.Trim("\") -split "\\")) {
        if (![string]::IsNullOrEmpty($segment)) {
            $current += "\" + $segment
            [PositronDeviceRapi]::EnsureDirectory($current)
        }
    }
}

function Receive-DebugLog([string] $remotePath, [string] $localPath)
{
    for ($attempt = 0; $attempt -lt 5; $attempt++) {
        try {
            if ([PositronDeviceRapi]::TryCopyFileFromDevice(
                    $remotePath, $localPath)) {
                return $true
            }
        } catch {
            if ($attempt -eq 4) {
                throw
            }
        }
        Start-Sleep -Milliseconds 250
    }
    return $false
}

function Invoke-ProcessCleanup([string] $root)
{
    $cleanupExe = $root + "\positron_process_cleanup.exe"
    Write-Capture "starting isolated process cleanup helper"
    $cleanupPid = [PositronDeviceRapi]::LaunchProcess(
            $cleanupExe, $root, $null)
    Write-Capture ("cleanup helper pid={0}" -f $cleanupPid)
    Start-Sleep -Milliseconds 500
}

function Copy-StageToDevice([string] $stagePath, [string] $root)
{
    $files = @(Get-ChildItem -LiteralPath $stagePath -Recurse -File |
        Sort-Object -Property FullName)
    if ($files.Count -eq 0) {
        throw "Local stage is empty: $stagePath"
    }

    Ensure-RemoteDirectoryTree $root
    foreach ($file in $files) {
        $relative = Get-RelativePath $stagePath $file.FullName
        $remotePath = $root + "\" + $relative
        $parent = Split-Path -Parent $remotePath
        Ensure-RemoteDirectoryTree $parent
    }

    $index = 0
    foreach ($file in $files) {
        $index++
        $relative = Get-RelativePath $stagePath $file.FullName
        $remotePath = $root + "\" + $relative
        $copied = $false
        for ($attempt = 1; $attempt -le 2; $attempt++) {
            try {
                Write-Capture ("copying {0}/{1}: {2}" -f
                        $index, $files.Count, $relative)
                [PositronDeviceRapi]::CopyFileToDevice(
                        $file.FullName, $remotePath)
                $copied = $true
                break
            } catch {
                $message = $_.Exception.ToString()
                $retryable = $message -match
                        "RAPI=0x80072746|RAPI=0x80072775|device=5"
                if (!$retryable -or $attempt -eq 2) {
                    throw
                }
                Write-Capture ("reconnecting after transient copy failure: " +
                        $relative)
                [PositronDeviceRapi]::Disconnect()
                Start-Sleep -Milliseconds 300
                [PositronDeviceRapi]::Connect()
            }
        }
        if (!$copied) {
            throw "Copy did not complete: $relative"
        }
    }
    return $files.Count
}

if ([Environment]::Is64BitProcess) {
    throw "Run scripts\debug_capture.bat so the 32-bit WMDC RAPI client is used."
}
if ($FollowSeconds -lt 0) {
    throw "FollowSeconds cannot be negative."
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Get-RunRoot $LocalRunRoot $runStamp
$localLog = Join-Path $runRoot "positron-debug.log"
$remoteLog = "\Temp\positron-debug.log"

if ([string]::IsNullOrEmpty($RemoteRoot)) {
    if ($PullOnly) {
        throw "-RemoteRoot is required with -PullOnly."
    }
    $RemoteRoot = "\Storage Card\Temp\Positron-device-gate\debug-capture-" +
            $runStamp
}
if ($RemoteRoot -notmatch
        "^\\Storage Card\\Temp\\Positron-device-gate\\[A-Za-z0-9._-]+$") {
    throw "Refusing unexpected remote root: $RemoteRoot"
}

New-Item -ItemType Directory -Path $runRoot -Force | Out-Null

if (!$PullOnly) {
    $stageScript = Join-Path $repoRoot "scripts\stage.bat"
    $stagePath = Join-Path $runRoot "stage"
    Write-Capture ("staging $Configuration into $stagePath")
    Push-Location $repoRoot
    try {
        & $stageScript $Configuration $stagePath
        $stageExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($stageExit -ne 0) {
        throw "scripts\stage.bat failed with exit code $stageExit."
    }
    if (!(Test-Path -LiteralPath (Join-Path $stagePath "positron.exe") -PathType Leaf)) {
        throw "Staged payload is missing positron.exe: $stagePath"
    }
}

$deviceGate = Join-Path $repoRoot "scripts\device_gate.ps1"
$deviceGateText = Get-Content -LiteralPath $deviceGate -Raw -Encoding UTF8
$rapiMatch = [regex]::Match($deviceGateText,
        "(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$rapiMatch.Success) {
    throw "Could not extract the RAPI helper from scripts\device_gate.ps1."
}
Add-Type -TypeDefinition $rapiMatch.Groups[1].Value -Language CSharp

$connected = $false
$remotePid = $null
$logFound = $false
$lastLogLength = -1

try {
    Write-Capture "connecting to the current WMDC device"
    [PositronDeviceRapi]::Connect()
    $connected = $true

    if (!$PullOnly) {
        Ensure-RemoteDirectoryTree "\Storage Card\Temp\Positron-device-gate"
        $fileCount = Copy-StageToDevice $stagePath $RemoteRoot
        Write-Capture ("deployed {0} files to {1}" -f $fileCount, $RemoteRoot)

        if ($ForceTerminatePositron) {
            Invoke-ProcessCleanup $RemoteRoot
        }

        [PositronDeviceRapi]::DeleteFileIfExists($remoteLog)
        $remoteExe = $RemoteRoot + "\positron.exe"
        Write-Capture ("launching {0}" -f $remoteExe)
        $remotePid = [PositronDeviceRapi]::LaunchProcess(
                $remoteExe, $RemoteRoot, $null)
        Write-Capture ("remote pid={0}" -f $remotePid)
    }

    $deadline = (Get-Date).AddSeconds($FollowSeconds)
    do {
        $logFound = Receive-DebugLog $remoteLog $localLog
        if ($logFound) {
            $lastLogLength = (Get-Item -LiteralPath $localLog).Length
            Write-Capture ("log snapshot: {0} bytes -> {1}" -f
                    $lastLogLength, $localLog)
        } elseif ($FollowSeconds -eq 0) {
            Write-Capture "log is not available yet; exercise the app and run -PullOnly to capture it"
        }
        if ((Get-Date) -ge $deadline -or $FollowSeconds -eq 0) {
            break
        }
        Start-Sleep -Seconds 1
    } while ($true)

    Write-Capture ("LOCAL_RUN_ROOT={0}" -f $runRoot)
    Write-Capture ("REMOTE_ROOT={0}" -f $RemoteRoot)
    Write-Capture ("REMOTE_LOG={0}" -f $remoteLog)
    Write-Capture ("LOCAL_LOG={0}" -f $localLog)
    if ($null -ne $remotePid) {
        Write-Capture ("REMOTE_PID={0}" -f $remotePid)
    }
} finally {
    if ($connected) {
        [PositronDeviceRapi]::Disconnect()
    }
}

exit 0
