param(
    [string] $Candidate = "next222",
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug",
    [int] $TimeoutSeconds = 1200,
    [string] $RemoteBase = "\Temp\Positron-device-gate",
    [string] $TestSelection = "",
    [switch] $EnableJavaScript,
    [string] $PlatformName = "",
    [string] $DeviceName = ""
)

$ErrorActionPreference = "Stop"

function Write-Stage([string] $message)
{
    Write-Host ("[device-gate] " + $message)
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
        [string] $exitCode,
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

function Get-CompletionMarker([string] $logText)
{
    if ($logText -match "(?m)^\[INFO\] TESTBENCH PASS\s*$") {
        return "PASS"
    }
    if ($logText -match "(?m)^\[ERROR\] TESTBENCH FAIL\s*$") {
        return "FAIL"
    }
    return "none"
}

function Receive-CompleteRemoteLog(
        [string] $remotePath,
        [string] $localPath)
{
    $firstText = $null
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(
                $remotePath, $localPath)) {
            return $false
        }
        $text = Get-Content -LiteralPath $localPath -Raw -Encoding UTF8
        if ((Get-CompletionMarker $text) -eq "none") {
            return $false
        }
        if ($attempt -eq 0) {
            $firstText = $text
            Start-Sleep -Milliseconds 250
        } elseif ($text -ne $firstText) {
            return $false
        }
    }
    return $true
}

function Remove-RemoteDirectorySafely([string] $remotePath)
{
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        try {
            [PositronDeviceRapi]::DeleteDirectoryTree($remotePath)
            return $true
        } catch {
            if ($attempt -eq 0) {
                Start-Sleep -Milliseconds 250
            }
        }
    }
    return $false
}

function Remove-RemoteDirectoryBestEffort([string] $remotePath)
{
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        try {
            if ([PositronDeviceRapi]::DeleteDirectoryTreeBestEffort(
                    $remotePath)) {
                return $true
            }
        } catch {
            if ($attempt -eq 0) {
                Start-Sleep -Milliseconds 250
            }
        }
    }
    return $false
}

if ($Candidate -notmatch "^[A-Za-z0-9][A-Za-z0-9._-]*$") {
    throw "Candidate must contain only letters, digits, dot, underscore or dash."
}
if ($TimeoutSeconds -lt 30) {
    throw "TimeoutSeconds must be at least 30."
}
if ($RemoteBase -notmatch "^\\[^\\]+\\[^\\]+" -or
        $RemoteBase -match "(^|\\)\.\.?($|\\)") {
    throw "RemoteBase must be an absolute device path with at least two non-dot segments."
}
if (![Environment]::Is64BitOperatingSystem -or
        [Environment]::Is64BitProcess) {
    throw "Run scripts\device_gate.bat so the 32-bit WMDC RAPI client is used."
}
if (![string]::IsNullOrEmpty($PlatformName) -or
        ![string]::IsNullOrEmpty($DeviceName)) {
    throw "-PlatformName and -DeviceName are not valid for the RAPI gate. RAPI always consumes WMDC's current connected device and never selects a VMID or target."
}
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$stageScript = Join-Path $PSScriptRoot "stage.bat"

$runStamp = Get-Date -Format "yyyyMMdd-HHmmss"
$runRoot = Join-Path $repoRoot ("tmp\device-runs\" + $runStamp + "-" + $Candidate)
$localStage = Join-Path $runRoot "stage"
$localLog = Join-Path $runRoot "test_host.log"
$resultPath = Join-Path $runRoot "device-gate-result.txt"
$manifestPath = Join-Path $runRoot "payload-sha256.txt"
$preflightPath = Join-Path $runRoot "device-gate-preflight.txt"
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
    "positron_browser.dll",
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

$stagedIni = Join-Path $localStage "test_host.ini"
if (![string]::IsNullOrEmpty($TestSelection)) {
    if ($TestSelection -match "[\r\n]") {
        throw "TestSelection must be a single tests= value."
    }
    $iniText = [IO.File]::ReadAllText($stagedIni, [Text.Encoding]::UTF8)
    if ($iniText -notmatch "(?m)^\s*tests\s*=.*$") {
        throw "The staged test_host.ini has no tests= line to override."
    }
    $iniText = [regex]::Replace($iniText,
            "(?m)^\s*tests\s*=.*$", "tests=" + $TestSelection, 1)
    [IO.File]::WriteAllText($stagedIni, $iniText,
            (New-Object Text.UTF8Encoding($false)))
    Write-Stage "staged test override: $TestSelection"
}
if ($EnableJavaScript) {
    $iniText = [IO.File]::ReadAllText($stagedIni, [Text.Encoding]::UTF8)
    if ($iniText -notmatch "(?m)^\s*javascript\s*=") {
        throw "The staged test_host.ini has no javascript= line to override."
    }
    $iniText = [regex]::Replace($iniText,
            "(?m)^\s*javascript\s*=.*$", "javascript=1", 1)
    [IO.File]::WriteAllText($stagedIni, $iniText,
            (New-Object Text.UTF8Encoding($false)))
    Write-Stage "staged browser JavaScript override: javascript=1"
}

$expectedTests = Get-ConfiguredTests $stagedIni
$payloadFiles = @(Get-ChildItem -LiteralPath $localStage -Recurse -File)
$manifest = foreach ($file in ($payloadFiles | Sort-Object FullName)) {
    $relative = Get-RelativePath $localStage $file.FullName
    $hash = Get-Sha256 $file.FullName
    "$hash  $relative"
}
Set-Content -LiteralPath $manifestPath -Value $manifest -Encoding ASCII

$payloadBytesValue = ($payloadFiles | Measure-Object -Property Length -Sum).Sum
if ($null -eq $payloadBytesValue) {
    $payloadBytesValue = 0
}
$storagePayloadBytes = [uint64] $payloadBytesValue
$storageReserveBytes = [uint64] 1048576
$storageRequiredBytes = $storagePayloadBytes + $storageReserveBytes
$internalCacheReserveBytes = [uint64] 65536
$priorEvidenceRoot = Join-Path $runRoot "prior-logs"
New-Item -ItemType Directory -Path $priorEvidenceRoot -Force | Out-Null

$rapiSource = @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;

public static class PositronDeviceRapi
{
    private const uint GENERIC_READ = 0x80000000;
    private const uint GENERIC_WRITE = 0x40000000;
    private const uint FILE_SHARE_READ = 0x00000001;
    private const uint FILE_SHARE_WRITE = 0x00000002;
    private const uint CREATE_ALWAYS = 2;
    private const uint OPEN_EXISTING = 3;
    private const uint FILE_ATTRIBUTE_DIRECTORY = 0x00000010;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x00000080;
    private const uint INVALID_FILE_ATTRIBUTES = 0xffffffff;

    [StructLayout(LayoutKind.Sequential)]
    private struct StartupInfo
    {
        public uint cb;
        public IntPtr lpReserved;
        public IntPtr lpDesktop;
        public IntPtr lpTitle;
        public uint dwX;
        public uint dwY;
        public uint dwXSize;
        public uint dwYSize;
        public uint dwXCountChars;
        public uint dwYCountChars;
        public uint dwFillAttribute;
        public uint dwFlags;
        public ushort wShowWindow;
        public ushort cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public uint dwProcessId;
        public uint dwThreadId;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct FileTime
    {
        public uint dwLowDateTime;
        public uint dwHighDateTime;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct CeFindData
    {
        public uint dwFileAttributes;
        public FileTime ftCreationTime;
        public FileTime ftLastAccessTime;
        public FileTime ftLastWriteTime;
        public uint nFileSizeHigh;
        public uint nFileSizeLow;
        public uint dwOID;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string cFileName;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RapiInit
    {
        public uint cbSize;
        public IntPtr heRapiInit;
        public int hrRapiInit;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct StoreInformation
    {
        public uint dwStoreSize;
        public uint dwFreeSize;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct LargeInteger
    {
        public uint LowPart;
        public uint HighPart;

        public ulong ToUInt64()
        {
            return ((ulong) HighPart << 32) | LowPart;
        }
    }

    public sealed class StorageProbe
    {
        public bool Available;
        public bool PathSpecific;
        public string Api;
        public string Scope;
        public ulong FreeBytes;
        public ulong TotalBytes;
        public ulong TotalFreeBytes;

        public StorageProbe(bool available, bool pathSpecific,
                string api, string scope, ulong freeBytes,
                ulong totalBytes, ulong totalFreeBytes)
        {
            Available = available;
            PathSpecific = pathSpecific;
            Api = api;
            Scope = scope;
            FreeBytes = freeBytes;
            TotalBytes = totalBytes;
            TotalFreeBytes = totalFreeBytes;
        }
    }

    private const uint WAIT_OBJECT_0 = 0x00000000;
    private const uint WAIT_TIMEOUT = 0x00000102;
    private const uint WAIT_FAILED = 0xffffffff;
    private const uint RAPI_INIT_TIMEOUT_MS = 30000;

    [DllImport("rapi.dll", ExactSpelling = true)]
    private static extern int CeRapiInitEx(ref RapiInit init);

    [DllImport("rapi.dll", ExactSpelling = true)]
    private static extern int CeRapiUninit();

    [DllImport("rapi.dll", ExactSpelling = true)]
    private static extern int CeRapiGetError();

    [DllImport("rapi.dll", ExactSpelling = true)]
    private static extern uint CeGetLastError();

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeGetDiskFreeSpaceEx(
        string directoryName, out LargeInteger freeBytesAvailableToCaller,
        out LargeInteger totalNumberOfBytes,
        out LargeInteger totalNumberOfFreeBytes);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeGetStoreInformation(
        out StoreInformation storeInformation);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint WaitForSingleObject(
        IntPtr handle, uint milliseconds);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeCreateDirectory(
        string pathName, IntPtr securityAttributes);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern uint CeGetFileAttributes(string fileName);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern IntPtr CeCreateFile(
        string fileName, uint desiredAccess, uint shareMode,
        IntPtr securityAttributes, uint creationDisposition,
        uint flagsAndAttributes, IntPtr templateFile);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeReadFile(
        IntPtr file, [Out] byte[] buffer, uint bytesToRead,
        out uint bytesRead, IntPtr overlapped);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeWriteFile(
        IntPtr file, byte[] buffer, uint bytesToWrite,
        out uint bytesWritten, IntPtr overlapped);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeCloseHandle(IntPtr handle);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    private static extern IntPtr CeFindFirstFile(
        string fileName, out CeFindData findData);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeFindNextFile(
        IntPtr findHandle, out CeFindData findData);

    [DllImport("rapi.dll", ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeFindClose(IntPtr findHandle);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeDeleteFile(string fileName);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeSetFileAttributes(
        string fileName, uint fileAttributes);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeRemoveDirectory(string pathName);

    [DllImport("rapi.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CeCreateProcess(
        string imageName, string commandLine,
        IntPtr processAttributes, IntPtr threadAttributes,
        [MarshalAs(UnmanagedType.Bool)] bool inheritHandles,
        uint creationFlags, IntPtr environment, string currentDirectory,
        ref StartupInfo startupInfo,
        out ProcessInformation processInformation);

    private static bool IsInvalidHandle(IntPtr handle)
    {
        return handle == new IntPtr(-1);
    }

    private static IOException CreateRemoteException(string operation)
    {
        uint deviceError = CeGetLastError();
        uint rapiError = unchecked((uint) CeRapiGetError());
        return new IOException(String.Format(
            "{0} failed (RAPI=0x{1:x8}, device={2}).",
            operation, rapiError, deviceError));
    }

    public static void Connect()
    {
        RapiInit init = new RapiInit();
        uint waitResult;
        int result;

        init.cbSize = (uint) Marshal.SizeOf(typeof(RapiInit));
        // CeRapiInitEx creates this event and returns its handle.  Supplying
        // or waiting on a caller-created event makes initialization hang.
        init.heRapiInit = IntPtr.Zero;
        init.hrRapiInit = 0;
        result = CeRapiInitEx(ref init);
        if (result < 0) {
            throw new InvalidOperationException(String.Format(
                "CeRapiInitEx failed (HRESULT=0x{0:x8}).",
                unchecked((uint) result)));
        }
        if (init.heRapiInit == IntPtr.Zero) {
            CeRapiUninit();
            throw new InvalidOperationException(
                "CeRapiInitEx succeeded without returning an event handle.");
        }
        waitResult = WaitForSingleObject(init.heRapiInit,
                RAPI_INIT_TIMEOUT_MS);
        if (waitResult == WAIT_TIMEOUT) {
            CeRapiUninit();
            throw new TimeoutException(
                "CeRapiInitEx timed out after 30 seconds; WMDC's current " +
                "RAPI session did not become ready.");
        }
        if (waitResult == WAIT_FAILED) {
            int error = Marshal.GetLastWin32Error();
            CeRapiUninit();
            throw new InvalidOperationException(String.Format(
                "WaitForSingleObject(CeRapiInitEx) failed (Win32={0}).",
                error));
        }
        if (waitResult != WAIT_OBJECT_0) {
            CeRapiUninit();
            throw new InvalidOperationException(String.Format(
                "WaitForSingleObject(CeRapiInitEx) returned 0x{0:x8}.",
                waitResult));
        }
        if (init.hrRapiInit < 0) {
            CeRapiUninit();
            throw new InvalidOperationException(String.Format(
                "CeRapiInitEx completed with HRESULT=0x{0:x8}.",
                unchecked((uint) init.hrRapiInit)));
        }
    }

    public static void Disconnect()
    {
        CeRapiUninit();
    }

    public static StorageProbe QueryVolumeStorage(string directoryName)
    {
        LargeInteger freeBytesAvailableToCaller;
        LargeInteger totalNumberOfBytes;
        LargeInteger totalNumberOfFreeBytes;

        try {
            if (CeGetDiskFreeSpaceEx(directoryName,
                    out freeBytesAvailableToCaller,
                    out totalNumberOfBytes,
                    out totalNumberOfFreeBytes)) {
                return new StorageProbe(true, true,
                    "CeGetDiskFreeSpaceEx", "volume",
                    freeBytesAvailableToCaller.ToUInt64(),
                    totalNumberOfBytes.ToUInt64(),
                    totalNumberOfFreeBytes.ToUInt64());
            }
        } catch (EntryPointNotFoundException) {
            // Older ActiveSync/RAPI builds do not export this API.
        } catch (MissingMethodException) {
            // Keep the explicit unavailable result for old CLR/RAPI combinations.
        }

        return new StorageProbe(false, false, "unavailable", "volume",
            0, 0, 0);
    }

    public static StorageProbe QueryObjectStoreStorage()
    {
        StoreInformation storeInformation;

        try {
            if (CeGetStoreInformation(out storeInformation)) {
                return new StorageProbe(true, false,
                    "CeGetStoreInformation", "object-store",
                    storeInformation.dwFreeSize,
                    storeInformation.dwStoreSize,
                    storeInformation.dwFreeSize);
            }
        } catch (EntryPointNotFoundException) {
            // Fall through to an explicit unavailable result.
        } catch (MissingMethodException) {
            // Fall through to an explicit unavailable result.
        }

        return new StorageProbe(false, false, "unavailable", "unknown",
            0, 0, 0);
    }

    public static StorageProbe QueryStorage(string directoryName)
    {
        StorageProbe volume = QueryVolumeStorage(directoryName);
        if (volume.Available) {
            return volume;
        }
        return QueryObjectStoreStorage();
    }

    public static void EnsureDirectory(string path)
    {
        if (CeCreateDirectory(path, IntPtr.Zero)) {
            return;
        }
        uint attributes = CeGetFileAttributes(path);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return;
        }
        throw CreateRemoteException("CeCreateDirectory(" + path + ")");
    }

    public static void CopyFileToDevice(string localPath, string remotePath)
    {
        IntPtr remote = CeCreateFile(remotePath, GENERIC_WRITE, 0,
            IntPtr.Zero, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
        if (IsInvalidHandle(remote)) {
            throw CreateRemoteException("CeCreateFile(" + remotePath + ")");
        }
        try {
            using (FileStream local = new FileStream(localPath, FileMode.Open,
                    FileAccess.Read, FileShare.Read)) {
                byte[] buffer = new byte[32768];
                int count;
                while ((count = local.Read(buffer, 0, buffer.Length)) > 0) {
                    uint written;
                    if (!CeWriteFile(remote, buffer, (uint) count,
                            out written, IntPtr.Zero) || written != (uint) count) {
                        throw CreateRemoteException(
                            "CeWriteFile(" + remotePath + ")");
                    }
                }
            }
        }
        finally {
            CeCloseHandle(remote);
        }
    }

    public static bool TryCopyFileFromDevice(
        string remotePath, string localPath)
    {
        IntPtr remote = CeCreateFile(remotePath, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE, IntPtr.Zero, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, IntPtr.Zero);
        if (IsInvalidHandle(remote)) {
            return false;
        }
        try {
            using (FileStream local = new FileStream(localPath,
                    FileMode.Create, FileAccess.Write, FileShare.Read)) {
                byte[] buffer = new byte[32768];
                while (true) {
                    uint read;
                    if (!CeReadFile(remote, buffer, (uint) buffer.Length,
                            out read, IntPtr.Zero)) {
                        throw CreateRemoteException(
                            "CeReadFile(" + remotePath + ")");
                    }
                    if (read == 0) {
                        break;
                    }
                    local.Write(buffer, 0, (int) read);
                }
            }
            return true;
        }
        finally {
            CeCloseHandle(remote);
        }
    }

    public static uint LaunchProcess(
        string imageName, string currentDirectory)
    {
        StartupInfo startupInfo = new StartupInfo();
        startupInfo.cb = (uint) Marshal.SizeOf(typeof(StartupInfo));
        ProcessInformation processInformation;
        if (!CeCreateProcess(imageName, null, IntPtr.Zero, IntPtr.Zero,
                false, 0, IntPtr.Zero, currentDirectory,
                ref startupInfo, out processInformation)) {
            throw CreateRemoteException("CeCreateProcess(" + imageName + ")");
        }
        try {
            return processInformation.dwProcessId;
        }
        finally {
            if (processInformation.hThread != IntPtr.Zero) {
                CeCloseHandle(processInformation.hThread);
            }
            if (processInformation.hProcess != IntPtr.Zero) {
                CeCloseHandle(processInformation.hProcess);
            }
        }
    }

    private static List<CeFindData> FindChildren(string directory)
    {
        List<CeFindData> children = new List<CeFindData>();
        CeFindData data;
        IntPtr find = CeFindFirstFile(
            directory.TrimEnd('\\') + "\\*", out data);
        if (IsInvalidHandle(find)) {
            uint error = CeGetLastError();
            if (error == 2 || error == 18) {
                return children;
            }
            throw CreateRemoteException(
                "CeFindFirstFile(" + directory + ")");
        }
        try {
            do {
                if (data.cFileName != "." && data.cFileName != "..") {
                    children.Add(data);
                }
            } while (CeFindNextFile(find, out data));
        }
        finally {
            CeFindClose(find);
        }
        return children;
    }

    public static string[] ListSubdirectories(string directory)
    {
        List<string> names = new List<string>();
        foreach (CeFindData child in FindChildren(directory)) {
            if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                names.Add(child.cFileName);
            }
        }
        return names.ToArray();
    }

    public static void DeleteDirectoryTree(string directory)
    {
        foreach (CeFindData child in FindChildren(directory)) {
            string path = directory.TrimEnd('\\') + "\\" + child.cFileName;
            if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                DeleteDirectoryTree(path);
            } else if (!CeDeleteFile(path)) {
                throw CreateRemoteException("CeDeleteFile(" + path + ")");
            }
        }
        if (!CeRemoveDirectory(directory)) {
            throw CreateRemoteException("CeRemoveDirectory(" + directory + ")");
        }
    }

    private static bool TryClearFileAttributes(string path)
    {
        try {
            return CeSetFileAttributes(path, FILE_ATTRIBUTE_NORMAL);
        } catch (EntryPointNotFoundException) {
            return false;
        } catch (MissingMethodException) {
            return false;
        }
    }

    public static bool DeleteDirectoryTreeBestEffort(string directory)
    {
        bool complete = true;
        foreach (CeFindData child in FindChildren(directory)) {
            string path = directory.TrimEnd('\\') + "\\" + child.cFileName;
            if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                if (!DeleteDirectoryTreeBestEffort(path)) {
                    complete = false;
                }
            } else if (!CeDeleteFile(path)) {
                if (!TryClearFileAttributes(path) ||
                        !CeDeleteFile(path)) {
                    complete = false;
                }
            }
        }
        if (!CeRemoveDirectory(directory)) {
            if (!TryClearFileAttributes(directory) ||
                    !CeRemoveDirectory(directory)) {
                complete = false;
            }
        }
        return complete;
    }
}
'@
Add-Type -TypeDefinition $rapiSource -Language CSharp

$targetDescription = "WMDC current RAPI connection"
try {
    $wmdc = Get-ItemProperty -LiteralPath `
            "HKCU:\Software\Microsoft\Windows CE Services" -ErrorAction Stop
    if (![string]::IsNullOrEmpty([string] $wmdc.DeviceOemInfo)) {
        $targetDescription += " / " + [string] $wmdc.DeviceOemInfo
    }
} catch {
    Write-Stage "WMDC target metadata is unavailable; RAPI remains authoritative"
}

$remoteRoot = $RemoteBase.TrimEnd("\") + "\" + $Candidate + "-" + $runStamp
$remoteExe = $remoteRoot + "\test_host.exe"
$remoteLog = $remoteRoot + "\test_host.log"
$remoteProcessId = 0
$timedOut = $false
$remoteExitCode = "not_exposed_by_rapi"
$completionMarker = "none"
$checkLines = @()
$rapiConnected = $false
$storageApi = "not_queried"
$storageScope = "unknown"
$storageFreeBytes = [uint64] 0
$storageTotalBytes = [uint64] 0
$storageTotalFreeBytes = [uint64] 0
$storageCheck = "not_run"
$targetStorageApi = "not_queried"
$targetStorageScope = "unknown"
$targetStorageFreeBytes = [uint64] 0
$targetStorageTotalBytes = [uint64] 0
$targetStorageTotalFreeBytes = [uint64] 0
$targetStorageCheck = "not_run"
$internalStorageApi = "not_queried"
$internalStorageScope = "unknown"
$internalStorageFreeBytes = [uint64] 0
$internalStorageTotalBytes = [uint64] 0
$internalStorageTotalFreeBytes = [uint64] 0
$internalStorageCheck = "not_run"
$priorCleanupRemoved = @()
$priorCleanupPreserved = @()
$spaceReclaimAttempted = $false
$spaceReclaimRemoved = @()
$spaceReclaimPartial = @()
$spaceReclaimPreserved = @()
$spaceReclaimRecheck = "not_run"
$currentCleanup = "not_attempted"
$completeLogRetrieved = $false

try {
    Write-Stage "opening the current GUI-connected WMDC target"
    try {
        [PositronDeviceRapi]::Connect()
        $rapiConnected = $true
    } catch {
        if ($_.Exception.ToString() -match "8007007e") {
            throw "WMDC's 32-bit RAPI client could not load a required module (0x8007007E). scripts\device_gate.bat already audits and repairs the known legacy COM paths before launch. Run scripts\repair_wmdc_rapi.bat -AuditOnly; if it reports status=PASS, investigate a missing DLL dependency instead of rewriting the known registrations."
        }
        throw "Could not open WMDC's current RAPI connection. Confirm that exactly one device is already connected in the GUI; the gate never connects, selects, cradles, starts or resets a device. $($_.Exception.Message)"
    }
    Write-Stage "using existing target: $targetDescription"

    $orderedPayload = @($payloadFiles | Sort-Object @{Expression = {
        if ($_.Name -eq "test_host.exe") { 2 }
        elseif ($_.Name -eq "test_host.ini") { 1 }
        else { 0 }
    }}, FullName)
    $remoteDirectories = New-Object "System.Collections.Generic.HashSet[string]"
    [void] $remoteDirectories.Add($remoteRoot)
    foreach ($file in $orderedPayload) {
        $relative = Get-RelativePath $localStage $file.FullName
        $remotePath = $remoteRoot + "\" + $relative
        $remoteDirectory = Split-Path -Parent $remotePath
        if (![string]::IsNullOrEmpty($remoteDirectory)) {
            $currentDirectory = ""
            foreach ($segment in ($remoteDirectory.Trim("\") -split "\\")) {
                if (![string]::IsNullOrEmpty($segment)) {
                    $currentDirectory += "\" + $segment
                    [void] $remoteDirectories.Add($currentDirectory)
                }
            }
        }
    }
    $remoteOwnerRoot = $RemoteBase.TrimEnd("\")
    $ownerDirectory = ""
    foreach ($segment in ($remoteOwnerRoot.Trim("\") -split "\\")) {
        if (![string]::IsNullOrEmpty($segment)) {
            $ownerDirectory += "\" + $segment
            Write-Stage "ensuring remote owner directory: $ownerDirectory"
            [PositronDeviceRapi]::EnsureDirectory($ownerDirectory)
        }
    }
    Write-Stage "checking prior gate directories under $remoteOwnerRoot"
    foreach ($oldName in [PositronDeviceRapi]::ListSubdirectories(
            $remoteOwnerRoot)) {
        $oldPath = $remoteOwnerRoot + "\" + $oldName
        if ($oldPath -eq $remoteRoot) {
            continue
        }
        if ($oldName -notmatch
                "^[A-Za-z0-9][A-Za-z0-9._-]*-\d{8}-\d{6}$") {
            Write-Stage "preserving unrecognized remote directory: $oldPath"
            $priorCleanupPreserved += "$oldPath (unrecognized)"
            continue
        }

        $oldRemoteLog = $oldPath + "\test_host.log"
        $oldLocalLog = Join-Path $priorEvidenceRoot ($oldName + ".log")
        Write-Stage "retrieving prior gate log before cleanup: $oldRemoteLog"
        if (!(Receive-CompleteRemoteLog $oldRemoteLog $oldLocalLog)) {
            Write-Stage "preserving prior gate directory (log is missing or incomplete): $oldPath"
            $priorCleanupPreserved += "$oldPath (log incomplete)"
            continue
        }
        if (Remove-RemoteDirectorySafely $oldPath) {
            Write-Stage "removed prior gate directory after complete log retrieval: $oldPath"
            $priorCleanupRemoved += $oldPath
        } else {
            Write-Stage "preserving prior gate directory (cleanup failed): $oldPath"
            $priorCleanupPreserved += "$oldPath (cleanup failed)"
        }
    }

    $evaluateStorage = {
        $targetStorageCheck = "not_run"
        $internalStorageCheck = "not_run"
        $storageCheck = "not_run"
        $storageFailure = $null
        $targetInfo = [PositronDeviceRapi]::QueryVolumeStorage($remoteOwnerRoot)
        $internalInfo = [PositronDeviceRapi]::QueryObjectStoreStorage()
        $targetStorageApi = $targetInfo.Api
        $targetStorageScope = $targetInfo.Scope
        $targetStorageFreeBytes = [uint64] $targetInfo.FreeBytes
        $targetStorageTotalBytes = [uint64] $targetInfo.TotalBytes
        $targetStorageTotalFreeBytes = [uint64] $targetInfo.TotalFreeBytes
        $internalStorageApi = $internalInfo.Api
        $internalStorageScope = $internalInfo.Scope
        $internalStorageFreeBytes = [uint64] $internalInfo.FreeBytes
        $internalStorageTotalBytes = [uint64] $internalInfo.TotalBytes
        $internalStorageTotalFreeBytes = [uint64] $internalInfo.TotalFreeBytes

        $knownObjectStorePath = $remoteOwnerRoot -match
                '(?i)^\\(Temp|Windows|Program Files|My Documents|Application Data)(?:\\|$)'
        if (!$targetInfo.Available -and $knownObjectStorePath -and
                $internalInfo.Available) {
            $targetInfo = $internalInfo
            $targetStorageApi = $targetInfo.Api
            $targetStorageScope = $targetInfo.Scope
            $targetStorageFreeBytes = [uint64] $targetInfo.FreeBytes
            $targetStorageTotalBytes = [uint64] $targetInfo.TotalBytes
            $targetStorageTotalFreeBytes = [uint64] $targetInfo.TotalFreeBytes
            Write-Stage "target volume API unavailable; using the internal object-store probe for the known object-store path"
        }

        $storageApi = $targetStorageApi
        $storageScope = $targetStorageScope
        $storageFreeBytes = $targetStorageFreeBytes
        $storageTotalBytes = $targetStorageTotalBytes
        $storageTotalFreeBytes = $targetStorageTotalFreeBytes
        Write-Stage (("target storage preflight: api={0} scope={1} free={2} " +
                "required={3} total={4}") -f $targetStorageApi,
                $targetStorageScope, $targetStorageFreeBytes,
                $storageRequiredBytes, $targetStorageTotalBytes)
        Write-Stage (("internal object-store preflight: api={0} free={1} " +
                "cache_reserve={2} total={3}") -f $internalStorageApi,
                $internalStorageFreeBytes, $internalCacheReserveBytes,
                $internalStorageTotalBytes)
        if (!$targetInfo.Available) {
            if ($knownObjectStorePath -and !$internalInfo.Available) {
                $targetStorageCheck = "UNAVAILABLE"
                $storageCheck = "UNAVAILABLE"
                $storageFailure = ("Could not query free device storage for {0}; the gate " +
                    "retrieved complete prior logs before cleanup but will not " +
                    "deploy without a storage API.") -f $remoteOwnerRoot
            } else {
                $targetStorageCheck = "UNAVAILABLE_PATH_SCOPE"
                $storageCheck = "UNAVAILABLE_PATH_SCOPE"
                $storageFailure = ("Could not query path-level free storage for {0}; " +
                    "the gate will not deploy an unknown or external target using " +
                    "only an object-store number.") -f $remoteOwnerRoot
            }
        } elseif (!$targetInfo.PathSpecific -and
                $remoteOwnerRoot -match '(?i)^\\Storage Card(?:\\|$)') {
            $targetStorageCheck = "UNAVAILABLE_PATH_SCOPE"
            $storageCheck = "UNAVAILABLE_PATH_SCOPE"
            $storageFailure = ("RAPI exposed only object-store free space while the deployment " +
                    "target is {0}; refusing a path-unsafe deployment.") -f $remoteOwnerRoot
        } elseif ($targetInfo.PathSpecific -and
                $targetStorageFreeBytes -lt $storageRequiredBytes) {
            $targetStorageCheck = "INSUFFICIENT_TARGET"
            $storageCheck = "INSUFFICIENT_TARGET"
            $storageFailure = ("Insufficient target-volume storage at {0}: free={1} bytes, " +
                    "required={2} bytes (payload={3}, reserve={4}, api={5}).") -f
                    $remoteOwnerRoot, $targetStorageFreeBytes, $storageRequiredBytes,
                    $storagePayloadBytes, $storageReserveBytes, $storageApi
        } elseif ($targetInfo.PathSpecific) {
            $targetStorageCheck = "PASS"
            if (!$internalInfo.Available) {
                $internalStorageCheck = "UNAVAILABLE_ADVISORY"
                $storageCheck = "PASS_TARGET_ONLY"
                Write-Stage "internal object-store probe unavailable; continuing on the path-specific target-volume check"
            } elseif ($internalStorageFreeBytes -lt $internalCacheReserveBytes) {
                $internalStorageCheck = "LOW_ADVISORY"
                $storageCheck = "PASS_WITH_INTERNAL_WARNING"
                Write-Stage "internal object-store space is below the cache advisory floor; target-volume capacity is sufficient, so deployment continues with a warning"
            } else {
                $internalStorageCheck = "PASS"
                $storageCheck = "PASS"
            }
        } elseif ($targetStorageFreeBytes -lt $storageRequiredBytes) {
            $targetStorageCheck = "INSUFFICIENT_OBJECT_STORE"
            $storageCheck = "INSUFFICIENT_OBJECT_STORE"
            $storageFailure = ("Insufficient object-store storage at {0}: free={1} bytes, " +
                    "required={2} bytes (payload={3}, reserve={4}, api={5}).") -f
                    $remoteOwnerRoot, $targetStorageFreeBytes, $storageRequiredBytes,
                    $storagePayloadBytes, $storageReserveBytes, $targetStorageApi
        } elseif (!$internalInfo.Available) {
            $targetStorageCheck = "PASS_COARSE"
            $internalStorageCheck = "UNAVAILABLE_ADVISORY"
            $storageCheck = "PASS_COARSE"
            Write-Stage "object-store fallback is available for the known internal target; cache probe is unavailable"
        } else {
            $targetStorageCheck = "PASS_COARSE"
            if ($internalStorageFreeBytes -lt $internalCacheReserveBytes) {
                $internalStorageCheck = "LOW_ADVISORY"
                $storageCheck = "PASS_COARSE_WITH_INTERNAL_WARNING"
                Write-Stage "internal object-store space is below the cache advisory floor; the coarse target check remains sufficient for this known internal path"
            } else {
                $internalStorageCheck = "PASS"
                $storageCheck = "PASS_COARSE"
            }
        }
        if (!$internalInfo.Available -and $internalStorageCheck -eq "not_run") {
            $internalStorageCheck = "UNAVAILABLE_ADVISORY"
            Write-Stage "internal object-store space could not be queried; this is advisory for an external target"
        } elseif ($internalInfo.Available -and
                $internalStorageCheck -eq "not_run") {
            if ($internalStorageFreeBytes -lt $internalCacheReserveBytes) {
                $internalStorageCheck = "LOW_ADVISORY"
            } else {
                $internalStorageCheck = "PASS"
            }
        }
        if ($storageCheck -eq "not_run") {
            $storageCheck = $targetStorageCheck
        }
    }
    . $evaluateStorage

    if ($null -ne $storageFailure -and
            ($targetStorageCheck -eq "INSUFFICIENT_TARGET" -or
             $targetStorageCheck -eq "INSUFFICIENT_OBJECT_STORE")) {
        $spaceReclaimAttempted = $true
        Write-Stage "target storage is insufficient; attempting emergency cleanup of stale gate directories"
        foreach ($oldName in [PositronDeviceRapi]::ListSubdirectories(
                $remoteOwnerRoot)) {
            $oldPath = $remoteOwnerRoot + "\" + $oldName
            if ($oldPath -eq $remoteRoot) {
                continue
            }
            if ($oldName -notmatch
                    "^[A-Za-z0-9][A-Za-z0-9._-]*-\d{8}-\d{6}$") {
                Write-Stage "preserving unrecognized remote directory during emergency cleanup: $oldPath"
                $spaceReclaimPreserved += "$oldPath (unrecognized)"
                continue
            }

            $oldRemoteLog = $oldPath + "\test_host.log"
            $oldLocalLog = Join-Path $priorEvidenceRoot ($oldName +
                    "-space-reclaim.log")
            Write-Stage "retrieving prior gate log before emergency space cleanup: $oldRemoteLog"
            $logComplete = Receive-CompleteRemoteLog $oldRemoteLog $oldLocalLog
            if ($logComplete) {
                Write-Stage "emergency cleanup captured a complete prior log: $oldPath"
            } else {
                Write-Stage "emergency cleanup could not capture a complete prior log; removing the stale directory to reclaim space: $oldPath"
            }
            if (Remove-RemoteDirectoryBestEffort $oldPath) {
                if ($logComplete) {
                    $spaceReclaimRemoved += "$oldPath (complete log)"
                } else {
                    $spaceReclaimRemoved += "$oldPath (forced; incomplete log best effort)"
                }
                Write-Stage "removed prior gate directory during emergency space cleanup: $oldPath"
            } else {
                Write-Stage "emergency cleanup removed what it could but a remote file or directory remains: $oldPath"
                $spaceReclaimPartial += "$oldPath (remote file or directory remains)"
            }
        }
        . $evaluateStorage
        $spaceReclaimRecheck = $storageCheck
        if ($null -eq $storageFailure) {
            Write-Stage "emergency cleanup reclaimed enough storage; deployment may continue"
        } else {
            Write-Stage "emergency cleanup did not reclaim enough storage; deployment remains blocked"
        }
    }
    $preflightLines = @(
        "remote_base=$remoteOwnerRoot",
        "storage_api=$storageApi",
        "storage_scope=$storageScope",
        "storage_free_bytes=$storageFreeBytes",
        "storage_total_free_bytes=$storageTotalFreeBytes",
        "storage_total_bytes=$storageTotalBytes",
        "storage_payload_bytes=$storagePayloadBytes",
        "storage_reserve_bytes=$storageReserveBytes",
        "storage_required_bytes=$storageRequiredBytes",
        "storage_check=$storageCheck",
        "target_storage_api=$targetStorageApi",
        "target_storage_scope=$targetStorageScope",
        "target_storage_free_bytes=$targetStorageFreeBytes",
        "target_storage_total_free_bytes=$targetStorageTotalFreeBytes",
        "target_storage_total_bytes=$targetStorageTotalBytes",
        "target_storage_check=$targetStorageCheck",
        "internal_storage_api=$internalStorageApi",
        "internal_storage_scope=$internalStorageScope",
        "internal_storage_free_bytes=$internalStorageFreeBytes",
        "internal_storage_total_free_bytes=$internalStorageTotalFreeBytes",
        "internal_storage_total_bytes=$internalStorageTotalBytes",
        "internal_cache_reserve_bytes=$internalCacheReserveBytes",
        "internal_storage_check=$internalStorageCheck",
        "prior_cleanup_removed_count=$($priorCleanupRemoved.Count)",
        "prior_cleanup_preserved_count=$($priorCleanupPreserved.Count)",
        "space_reclaim_attempted=$spaceReclaimAttempted",
        "space_reclaim_recheck=$spaceReclaimRecheck",
        "space_reclaim_removed_count=$($spaceReclaimRemoved.Count)",
        "space_reclaim_partial_count=$($spaceReclaimPartial.Count)",
        "space_reclaim_preserved_count=$($spaceReclaimPreserved.Count)"
    )
    if ($priorCleanupRemoved.Count -gt 0) {
        $preflightLines += "prior_cleanup_removed=$($priorCleanupRemoved -join '|')"
    }
    if ($priorCleanupPreserved.Count -gt 0) {
        $preflightLines += "prior_cleanup_preserved=$($priorCleanupPreserved -join '|')"
    }
    if ($spaceReclaimRemoved.Count -gt 0) {
        $preflightLines += "space_reclaim_removed=$($spaceReclaimRemoved -join '|')"
    }
    if ($spaceReclaimPreserved.Count -gt 0) {
        $preflightLines += "space_reclaim_preserved=$($spaceReclaimPreserved -join '|')"
    }
    if ($spaceReclaimPartial.Count -gt 0) {
        $preflightLines += "space_reclaim_partial=$($spaceReclaimPartial -join '|')"
    }
    Set-Content -LiteralPath $preflightPath -Value $preflightLines -Encoding UTF8
    if ($null -ne $storageFailure) {
        throw $storageFailure
    }
    foreach ($remoteDirectory in ($remoteDirectories | Sort-Object Length)) {
        Write-Stage "creating remote directory: $remoteDirectory"
        [PositronDeviceRapi]::EnsureDirectory($remoteDirectory)
    }
    $index = 0
    foreach ($file in $orderedPayload) {
        $index++
        $relative = Get-RelativePath $localStage $file.FullName
        $remotePath = $remoteRoot + "\" + $relative
        Write-Stage "deploying $index/$($orderedPayload.Count): $relative"
        [PositronDeviceRapi]::CopyFileToDevice($file.FullName, $remotePath)
    }

    Write-Stage "starting $remoteExe"
    $remoteProcessId = [PositronDeviceRapi]::LaunchProcess(
            $remoteExe, $remoteRoot)
    Write-Stage "started remote process id $remoteProcessId"

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($completionMarker -eq "none") {
        if ((Get-Date) -ge $deadline) {
            $timedOut = $true
            break
        }
        if ([PositronDeviceRapi]::TryCopyFileFromDevice(
                $remoteLog, $localLog)) {
            $partialLog = Get-Content -LiteralPath $localLog -Raw -Encoding UTF8
            $partialMarker = Get-CompletionMarker $partialLog
            if ($partialMarker -ne "none") {
                $completionMarker = $partialMarker
            }
        }
        if ($completionMarker -eq "none") {
            Start-Sleep -Seconds 1
        }
    }
    if ($timedOut) {
        [void] [PositronDeviceRapi]::TryCopyFileFromDevice(
                $remoteLog, $localLog)
        throw "The device gate timed out after $TimeoutSeconds seconds. RAPI 1 does not expose a safe remote wait/terminate API, so the gate did not kill any device process."
    }

    Start-Sleep -Milliseconds 500
    Write-Stage "receiving complete test_host.log"
    if (!(Receive-CompleteRemoteLog $remoteLog $localLog)) {
        throw "The completed remote test_host.log could not be received."
    }
    $completeLogRetrieved = $true
    $finalLogText = Get-Content -LiteralPath $localLog -Raw -Encoding UTF8
    $finalMarker = Get-CompletionMarker $finalLogText
    if ($finalMarker -ne $completionMarker) {
        throw ("The completed remote test_host.log changed its completion " +
                "marker from {0} to {1} while it was being received.") -f
                $completionMarker, $finalMarker
    }
    Start-Sleep -Milliseconds 250
    if (Remove-RemoteDirectorySafely $remoteRoot) {
        $currentCleanup = "removed_after_complete_log"
        Write-Stage "removed current gate directory after complete log retrieval: $remoteRoot"
    } else {
        $currentCleanup = "preserved_cleanup_failed"
        Write-Stage "preserving current gate directory because cleanup failed: $remoteRoot"
    }
} finally {
    if ($rapiConnected) {
        [PositronDeviceRapi]::Disconnect()
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
$checkLines += "completion_marker=$completionMarker"
$checkLines += "storage_api=$storageApi"
$checkLines += "storage_scope=$storageScope"
$checkLines += "storage_free_bytes=$storageFreeBytes"
$checkLines += "storage_total_free_bytes=$storageTotalFreeBytes"
$checkLines += "storage_total_bytes=$storageTotalBytes"
$checkLines += "storage_payload_bytes=$storagePayloadBytes"
$checkLines += "storage_reserve_bytes=$storageReserveBytes"
$checkLines += "storage_required_bytes=$storageRequiredBytes"
$checkLines += "storage_check=$storageCheck"
$checkLines += "target_storage_api=$targetStorageApi"
$checkLines += "target_storage_scope=$targetStorageScope"
$checkLines += "target_storage_free_bytes=$targetStorageFreeBytes"
$checkLines += "target_storage_total_free_bytes=$targetStorageTotalFreeBytes"
$checkLines += "target_storage_total_bytes=$targetStorageTotalBytes"
$checkLines += "target_storage_check=$targetStorageCheck"
$checkLines += "internal_storage_api=$internalStorageApi"
$checkLines += "internal_storage_scope=$internalStorageScope"
$checkLines += "internal_storage_free_bytes=$internalStorageFreeBytes"
$checkLines += "internal_storage_total_free_bytes=$internalStorageTotalFreeBytes"
$checkLines += "internal_storage_total_bytes=$internalStorageTotalBytes"
$checkLines += "internal_cache_reserve_bytes=$internalCacheReserveBytes"
$checkLines += "internal_storage_check=$internalStorageCheck"
$checkLines += "prior_cleanup_removed_count=$($priorCleanupRemoved.Count)"
$checkLines += "prior_cleanup_preserved_count=$($priorCleanupPreserved.Count)"
$checkLines += "space_reclaim_attempted=$spaceReclaimAttempted"
$checkLines += "space_reclaim_recheck=$spaceReclaimRecheck"
$checkLines += "space_reclaim_removed_count=$($spaceReclaimRemoved.Count)"
$checkLines += "space_reclaim_partial_count=$($spaceReclaimPartial.Count)"
$checkLines += "space_reclaim_preserved_count=$($spaceReclaimPreserved.Count)"
if ($spaceReclaimRemoved.Count -gt 0) {
    $checkLines += "space_reclaim_removed=$($spaceReclaimRemoved -join '|')"
}
if ($spaceReclaimPreserved.Count -gt 0) {
    $checkLines += "space_reclaim_preserved=$($spaceReclaimPreserved -join '|')"
}
if ($spaceReclaimPartial.Count -gt 0) {
    $checkLines += "space_reclaim_partial=$($spaceReclaimPartial -join '|')"
}
$checkLines += "current_cleanup=$currentCleanup"
$checkLines += "complete_log_retrieved=$completeLogRetrieved"

$storageGateOk = $storageCheck -match "^PASS"
$passed = $storageGateOk -and
        $completionMarker -eq "PASS" -and $metricOk -and
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
