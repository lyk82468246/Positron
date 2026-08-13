param(
    [string] $Candidate = "next220",
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug",
    [int] $TimeoutSeconds = 1200,
    [string] $RemoteBase = "\Temp\Positron-device-gate",
    [string] $PlatformName = "",
    [string] $DeviceName = ""
)

$ErrorActionPreference = "Stop"

function Write-Stage([string] $message)
{
    Write-Host ("[device-gate] " + $message)
}

function Normalize-GuidText([string] $value)
{
    return $value.Trim().TrimStart("{").TrimEnd("}").ToUpperInvariant()
}

function Get-ConfiguredTests([string] $iniPath)
{
    $selection = $null
    foreach ($line in Get-Content -LiteralPath $iniPath -Encoding UTF8) {
        $trimmed = $line.Trim()
        if ($trimmed.Length -eq 0 -or $trimmed.StartsWith("#") -or
                $trimmed.StartsWith(";")) {
            continue
        }
        if ($trimmed -match "^tests\s*=\s*(.+)$") {
            $selection = $Matches[1]
            break
        }
    }
    if ([string]::IsNullOrEmpty($selection)) {
        throw "No tests= selection was found in $iniPath."
    }

    $tests = New-Object "System.Collections.Generic.HashSet[string]"
    foreach ($token in ($selection -split "[,\s]+")) {
        if ([string]::IsNullOrEmpty($token)) {
            continue
        }
        if ($token -match "^(\d+)-(\d+)$") {
            $first = [int] $Matches[1]
            $last = [int] $Matches[2]
            if ($last -lt $first) {
                throw "Descending test range is invalid: $token."
            }
            for ($number = $first; $number -le $last; $number++) {
                [void] $tests.Add($number.ToString())
            }
        } elseif ($token -match "^\d+$") {
            [void] $tests.Add(([int] $token).ToString())
        } elseif ($token.ToLowerInvariant() -eq "7b") {
            [void] $tests.Add("7b")
        } else {
            throw "Invalid test selector: $token."
        }
    }
    return $tests
}

function Get-RelativePath([string] $root, [string] $path)
{
    $rootUri = New-Object Uri(($root.TrimEnd("\") + "\"))
    $pathUri = New-Object Uri($path)
    return [Uri]::UnescapeDataString(
            $rootUri.MakeRelativeUri($pathUri).ToString()).Replace("/", "\")
}

function Get-Sha256([string] $path)
{
    $sha256 = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try {
        $bytes = $sha256.ComputeHash($stream)
        return [BitConverter]::ToString($bytes).Replace("-", "")
    } finally {
        $stream.Dispose()
        $sha256.Dispose()
    }
}

function Write-ResultFile(
        [string] $path,
        [string] $status,
        [string] $target,
        [string] $remoteRoot,
        [int] $exitCode,
        [string[]] $checks)
{
    $lines = @(
        "status=$status",
        "target=$target",
        "remote_root=$remoteRoot",
        "remote_exit_code=$exitCode"
    )
    $lines += $checks
    Set-Content -LiteralPath $path -Value $lines -Encoding UTF8
}

if ($Candidate -notmatch "^[A-Za-z0-9][A-Za-z0-9._-]*$") {
    throw "Candidate must contain only letters, digits, dot, underscore or dash."
}
if ($TimeoutSeconds -lt 30) {
    throw "TimeoutSeconds must be at least 30."
}
if (![Environment]::Is64BitOperatingSystem -or
        [Environment]::Is64BitProcess) {
    throw "Run scripts\device_gate.bat so the 32-bit CoreCon client is used."
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$stageScript = Join-Path $PSScriptRoot "stage.bat"
$coreConPath = "C:\Program Files (x86)\Common Files\Microsoft Shared\CoreCon\1.0\Bin\Microsoft.Smartdevice.Connectivity.dll"
if (!(Test-Path -LiteralPath $coreConPath)) {
    throw "VS2008 CoreCon was not found at $coreConPath."
}

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $repoRoot ("tmp\device-runs\" + $runStamp + "-" + $Candidate)
$localStage = Join-Path $runRoot "stage"
$localLog = Join-Path $runRoot "test_host.log"
$resultPath = Join-Path $runRoot "device-gate-result.txt"
$manifestPath = Join-Path $runRoot "payload-sha256.txt"
New-Item -ItemType Directory -Path $localStage -Force | Out-Null

Write-Stage "building and staging $Candidate ($Configuration)"
& $stageScript $Configuration $localStage
if ($LASTEXITCODE -ne 0) {
    throw "scripts\stage.bat failed with exit code $LASTEXITCODE."
}

$required = @(
    "positron_tls.dll",
    "positron_json.dll",
    "positron_http.dll",
    "positron_core.dll",
    "positron_image.dll",
    "positron_script.dll",
    "test_host.exe",
    "test_host.ini",
    "fonts\PositronSymbolsBasic.ttf",
    "fonts\PositronSymbols.ttf",
    "fonts\PositronEmoji.ttf"
)
foreach ($relative in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $localStage $relative))) {
        throw "Staged payload is missing $relative."
    }
}

$expectedTests = Get-ConfiguredTests (Join-Path $localStage "test_host.ini")
$payloadFiles = @(Get-ChildItem -LiteralPath $localStage -Recurse -File)
$manifest = foreach ($file in ($payloadFiles | Sort-Object FullName)) {
    $relative = Get-RelativePath $localStage $file.FullName
    $hash = Get-Sha256 $file.FullName
    "$hash  $relative"
}
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding ASCII

Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class PositronProcessCommandLine
{
    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
    [DllImport("ntdll.dll")]
    private static extern int NtQueryInformationProcess(IntPtr handle,
            int infoClass, IntPtr info, int length, out int returned);

    public static string Read(uint processId)
    {
        IntPtr handle = OpenProcess(0x1000, false, processId);
        if (handle == IntPtr.Zero) {
            throw new Win32Exception();
        }
        try {
            int needed;
            int status = NtQueryInformationProcess(handle, 60,
                    IntPtr.Zero, 0, out needed);
            if (needed <= 0) {
                throw new InvalidOperationException(String.Format(
                        "Command-line size query failed: 0x{0:X8}", status));
            }
            IntPtr buffer = Marshal.AllocHGlobal(needed);
            try {
                status = NtQueryInformationProcess(handle, 60,
                        buffer, needed, out needed);
                if (status < 0) {
                    throw new InvalidOperationException(String.Format(
                            "Command-line query failed: 0x{0:X8}", status));
                }
                ushort bytes = (ushort) Marshal.ReadInt16(buffer, 0);
                IntPtr text = Marshal.ReadIntPtr(buffer,
                        IntPtr.Size == 8 ? 8 : 4);
                return Marshal.PtrToStringUni(text, bytes / 2);
            } finally {
                Marshal.FreeHGlobal(buffer);
            }
        } finally {
            CloseHandle(handle);
        }
    }
}
"@

[void] [Reflection.Assembly]::LoadFile($coreConPath)
$manager = New-Object Microsoft.SmartDevice.Connectivity.DatastoreManager 1033
$targetDevice = $null
$targetDescription = ""
$emulatorPid = 0

if (![string]::IsNullOrEmpty($DeviceName)) {
    $matches = @()
    foreach ($platform in $manager.GetPlatforms()) {
        if (![string]::IsNullOrEmpty($PlatformName) -and
                $platform.Name -ne $PlatformName) {
            continue
        }
        foreach ($device in $platform.GetDevices()) {
            if ($device.Name -eq $DeviceName) {
                $matches += $device
            }
        }
    }
    if ($matches.Count -ne 1) {
        throw "Expected exactly one datastore device named '$DeviceName'; found $($matches.Count). Supply -PlatformName when names are ambiguous."
    }
    $targetDevice = $matches[0]
    $targetDescription = $targetDevice.Name
} else {
    $emulators = @(Get-Process -Name DeviceEmulator -ErrorAction SilentlyContinue)
    if ($emulators.Count -ne 1) {
        throw "Expected exactly one already-running DeviceEmulator; found $($emulators.Count). This script never starts or selects a device."
    }
    $emulatorPid = $emulators[0].Id
    $commandLine = [PositronProcessCommandLine]::Read([uint32] $emulatorPid)
    if ($commandLine -notmatch "(?i)/VMID\s+\{?([0-9a-f-]{36})\}?") {
        throw "The running emulator command line has no VMID."
    }
    $targetId = Normalize-GuidText $Matches[1]
    foreach ($platform in $manager.GetPlatforms()) {
        foreach ($device in $platform.GetDevices()) {
            if ((Normalize-GuidText $device.Id.ToString()) -eq $targetId) {
                $targetDevice = $device
                $targetDescription = $device.Name + " [" + $targetId + "]"
                break
            }
        }
        if ($null -ne $targetDevice) {
            break
        }
    }
    if ($null -eq $targetDevice) {
        throw "The running emulator VMID $targetId is absent from the CoreCon datastore."
    }
}

$remoteRoot = $RemoteBase.TrimEnd("\") + "\" + $Candidate + "-" + $runStamp
$remoteExe = $remoteRoot + "\test_host.exe"
$remoteLog = $remoteRoot + "\test_host.log"
$remoteProcess = $null
$connected = $false
$timedOut = $false
$remoteExitCode = -1
$checkLines = @()

try {
    Write-Stage "opening CoreCon channel to existing target: $targetDescription"
    $targetDevice.Connect()
    $connected = $targetDevice.IsConnected()
    if (!$connected) {
        throw "CoreCon did not connect to the existing target."
    }
    if ($emulatorPid -ne 0) {
        $now = @(Get-Process -Name DeviceEmulator -ErrorAction SilentlyContinue)
        if ($now.Count -ne 1 -or $now[0].Id -ne $emulatorPid) {
            throw "The emulator set changed while opening CoreCon. Refusing to continue."
        }
    }

    $oldHosts = @($targetDevice.GetRunningProcesses() |
            Where-Object { $_.FileName -match "(?i)(^|\\)test_host\.exe$" })
    if ($oldHosts.Count -ne 0) {
        throw "A test_host.exe process is already running on the target. Close it and retry; the gate never kills a process it did not start."
    }

    $deployer = $targetDevice.GetFileDeployer()
    $orderedPayload = @($payloadFiles | Sort-Object @{Expression = {
        if ($_.Name -eq "test_host.exe") { 2 }
        elseif ($_.Name -eq "test_host.ini") { 1 }
        else { 0 }
    }}, FullName)
    $index = 0
    foreach ($file in $orderedPayload) {
        $index++
        $relative = Get-RelativePath $localStage $file.FullName
        $remotePath = $remoteRoot + "\" + $relative
        Write-Stage "deploying $index/$($orderedPayload.Count): $relative"
        $deployer.SendFile($file.FullName, $remotePath, $true, $false)
    }

    Write-Stage "starting $remoteExe"
    $remoteProcess = $targetDevice.GetRemoteProcess()
    if (!$remoteProcess.Start($remoteExe, "")) {
        throw "RemoteProcess.Start returned false for $remoteExe."
    }

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while (!$remoteProcess.HasExited()) {
        if ((Get-Date) -ge $deadline) {
            $timedOut = $true
            break
        }
        Start-Sleep -Seconds 1
    }
    if ($timedOut) {
        try {
            $deployer.ReceiveFile($remoteLog, $localLog, $true)
        } catch {
            Write-Stage "partial log was not available after timeout"
        }
        $remoteProcess.Kill()
        throw "The device gate timed out after $TimeoutSeconds seconds; only the process started by this run was killed."
    }

    $remoteExitCode = $remoteProcess.GetExitCode()
    Start-Sleep -Milliseconds 500
    Write-Stage "receiving complete test_host.log"
    $deployer.ReceiveFile($remoteLog, $localLog, $true)
} finally {
    if ($connected -and $targetDevice.IsConnected()) {
        $targetDevice.Disconnect()
    }
}

$logText = Get-Content -LiteralPath $localLog -Raw -Encoding UTF8
$metricOk = $logText -match "(?m)^Device metrics: screen=\d+x\d+ dpi=\d+\s*$"
$errorCount = ([regex]::Matches($logText, "(?m)^\[ERROR\]")).Count
$failCount = ([regex]::Matches($logText, "(?m)^\[[A-Z]+\].*\bFAIL\b")).Count
$passCount = ([regex]::Matches($logText, "(?m)^\[INFO\] TESTBENCH PASS\s*$")).Count

$actualTests = New-Object "System.Collections.Generic.HashSet[string]"
foreach ($match in [regex]::Matches($logText,
        "(?m)^\[INFO\] TEST (7b|\d+) OK(?:\b|\s|\()")) {
    [void] $actualTests.Add($match.Groups[1].Value.ToLowerInvariant())
}
$missing = @($expectedTests | Where-Object { !$actualTests.Contains($_) } |
        Sort-Object { if ($_ -eq "7b") { 7.5 } else { [double] $_ } })
$unexpected = @($actualTests | Where-Object { !$expectedTests.Contains($_) } |
        Sort-Object { if ($_ -eq "7b") { 7.5 } else { [double] $_ } })

$routeOk = $true
if ($expectedTests.Contains("13")) {
    $routes = @(
        "example\.com/",
        "www\.iana\.org/help/example-domains",
        "www\.iana\.org/domains/reserved"
    )
    foreach ($route in $routes) {
        if ($logText -notmatch ("(?ms)^\[INFO\] TEST 13 NAV " + $route +
                "\r?\ncompleted=1\b")) {
            $routeOk = $false
        }
    }
}

$checkLines += "metrics_ok=$metricOk"
$checkLines += "selected_test_count=$($expectedTests.Count)"
$checkLines += "observed_ok_test_count=$($actualTests.Count)"
$checkLines += "missing_tests=$($missing -join ',')"
$checkLines += "unexpected_tests=$($unexpected -join ',')"
$checkLines += "error_count=$errorCount"
$checkLines += "fail_count=$failCount"
$checkLines += "testbench_pass_count=$passCount"
$checkLines += "test13_route_ok=$routeOk"

$passed = $remoteExitCode -eq 0 -and $metricOk -and
        $missing.Count -eq 0 -and $unexpected.Count -eq 0 -and
        $errorCount -eq 0 -and $failCount -eq 0 -and
        $passCount -eq 1 -and $routeOk
$status = if ($passed) { "PASS" } else { "FAIL" }
Write-ResultFile $resultPath $status $targetDescription $remoteRoot `
        $remoteExitCode $checkLines

Write-Stage "result=$status evidence=$runRoot"
foreach ($line in $checkLines) {
    Write-Host ("  " + $line)
}
if (!$passed) {
    exit 1
}
exit 0
