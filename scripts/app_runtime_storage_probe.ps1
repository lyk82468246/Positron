# Explicit Debug probe of an existing failed two root; no database repair/retry.
param(
    [Parameter(Mandatory=$true)][string]$RunRoot,
    [Parameter(Mandatory=$true)][string]$DiagnosticStage,
    [ValidateLength(1,64)][ValidatePattern('^runtime-storage-[a-z0-9-]+$')][string]$EvidenceName='runtime-storage-probe'
)
$ErrorActionPreference='Stop'
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$stage=[IO.Path]::GetFullPath($DiagnosticStage)
$runs=Join-Path $repo 'tmp\device-runs\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-sql$' -or
        !$stage.StartsWith((Join-Path $repo 'tmp\'),[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Require an existing formal SQL run and a private formal diagnostic stage.'
}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing original formal deployment.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-runtime-sql-\d{8}-\d{6}$') { throw 'Unexpected remote root.' }
$evidence=Join-Path $run $EvidenceName
if (Test-Path $evidence) { throw 'Never overwrite evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$source=[regex]::Match((Get-Content $gatePath -Raw -Encoding UTF8),"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$source.Success) { throw 'Missing formal RAPI helper.' }
# Enumerate files with the same bounded read-only helper; do not guess storage keys.
$code=$source.Groups[1].Value.Replace('public static string[] ListSubdirectories(string directory)',@'
public static string[] ListFiles(string directory)
    {
        List<string> names = new List<string>();
        foreach (CeFindData child in FindChildren(directory)) {
            if ((child.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) names.Add(child.cFileName);
        }
        return names.ToArray();
    }
    public static string[] ListSubdirectories(string directory)
'@)
Add-Type -TypeDefinition $code -Language CSharp
function Import-Function([string]$path,[string]$name) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)
    if ($errors.Count -or !$fn) { throw ('Missing helper '+$name) }
    return $fn.Extent.Text
}
. ([scriptblock]::Create((Import-Function $gatePath 'Invoke-RemoteModuleAudit')))
. ([scriptblock]::Create((Import-Function (Join-Path $PSScriptRoot 'app_runtime_platform_gate.ps1') 'Wait-NormalExit')))
function Write-Stage([string]$message) { Write-Host $message }
function Assert-NoHolders([string]$tag) {
    [PositronDeviceRapi]::DeleteFileIfExists($remote+'\module-audit.log')
    [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') ($remote+'\module-audit.log') (Join-Path $evidence ($tag+'-modules.log')) 30)
}
$script:storageProbeEntries=0
function Copy-DataTree([string]$relative,[string]$destination,[int]$depth) {
    if ($depth -gt 5) { throw 'Unexpected storage tree depth.' }
    [void][IO.Directory]::CreateDirectory($destination)
    $directory=$remote+'\'+$relative
    $files=@([PositronDeviceRapi]::ListFiles($directory))
    $directories=@([PositronDeviceRapi]::ListSubdirectories($directory))
    $script:storageProbeEntries += $files.Count+$directories.Count
    if ($script:storageProbeEntries -gt 256) { throw 'Unexpected storage tree size.' }
    foreach ($name in $files) {
        if ($name -match '[\\/:]' -or $name -in @('.','..')) { throw 'Invalid enumerated file name.' }
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($directory+'\'+$name,(Join-Path $destination $name))) { throw ('Cold backup failed: '+$relative+'\'+$name) }
    }
    foreach ($name in $directories) {
        if ($name -match '[\\/:]' -or $name -in @('.','..')) { throw 'Invalid enumerated directory name.' }
        Copy-DataTree ($relative+'\'+$name) (Join-Path $destination $name) ($depth+1)
    }
}
function Hash-Tree([string]$directory,[string]$output) {
    $lines=@(Get-ChildItem $directory -File -Recurse | Sort-Object FullName | ForEach-Object {
        (Get-FileHash $_.FullName).Hash+'  '+$_.FullName.Substring($directory.Length+1)
    })
    [IO.File]::WriteAllLines($output,[string[]]$lines)
}
$connected=$false
try {
    [PositronDeviceRapi]::Connect(); $connected=$true
    Assert-NoHolders 'before'
    $space=[PositronDeviceRapi]::QueryVolumeStorage($remote)
    $internal=[PositronDeviceRapi]::QueryObjectStoreStorage()
    @($space,$internal) | ConvertTo-Json | Set-Content (Join-Path $evidence 'space.json') -Encoding UTF8
    $exe=Join-Path $stage 'positron.exe'
    if (![Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($exe)).Contains('runtime-storage phase=open-end')) { throw 'Require the formal Debug storage diagnostic EXE.' }
    if (!$space.Available -or $space.FreeBytes -lt ((Get-Item $exe).Length+1048576) -or
            !$internal.Available -or $internal.FreeBytes -lt 65536) { throw 'Storage preflight failed.' }
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object { $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    if ($bins.Count -ne 11) { throw 'Expected original EXE plus ten DLLs.' }
    foreach ($bin in $bins) {
        Write-Host ('Verifying original '+$bin.Name)
        $back=Join-Path $evidence $bin.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$back) -or
                (Get-FileHash $back).Hash -ne (Get-FileHash $bin.FullName).Hash) { throw ('Original binary mismatch: '+$bin.Name) }
        if ($bin.Extension -eq '.dll' -and (Get-FileHash $bin.FullName).Hash -ne
                (Get-FileHash (Join-Path $stage $bin.Name)).Hash) { throw ('Diagnostic DLL changed: '+$bin.Name) }
    }
    $fixture=Join-Path $repo 'positron_app\fixtures\runtime-sql'
    foreach ($file in Get-ChildItem $fixture -File -Recurse | Where-Object { $_.Name -ne 'seed-two.js' }) {
        $relative=$file.FullName.Substring($fixture.Length+1)
        $fixtureSource=$file.FullName
        if ($relative -eq 'seed.js') { $fixtureSource=Join-Path $fixture 'seed-two.js' }
        $back=Join-Path $evidence ('two-fixture-'+$file.Name)
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\fixture-two\'+$relative,$back) -or
                (Get-FileHash $back).Hash -ne (Get-FileHash $fixtureSource).Hash) { throw 'Original two fixture mismatch; never replace it.' }
    }
    $script:storageProbeEntries=0
    Copy-DataTree 'data' (Join-Path $evidence 'data-before') 0
    Hash-Tree (Join-Path $evidence 'data-before') (Join-Path $evidence 'data-before-sha256.txt')
    if (![PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',(Join-Path $evidence 'previous-debug.log')) -and
            [PositronDeviceRapi]::ListFiles('\Temp') -contains 'positron-debug.log') { throw 'Previous debug log could not be preserved.' }
    $crashBefore=[PositronDeviceRapi]::SnapshotCrashDumps()
    $image=$remote+'\positron-'+$EvidenceName+'.exe'
    # A separate filename preserves the failed EXE AND its original storage location.
    [PositronDeviceRapi]::CopyFileToDevice($exe,$image)
    $back=Join-Path $evidence 'positron-storage-diagnostic.exe'
    if (![PositronDeviceRapi]::TryCopyFileFromDevice($image,$back) -or
            (Get-FileHash $back).Hash -ne (Get-FileHash $exe).Hash) { throw 'Diagnostic EXE mismatch.' }
    [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
    $appPid=[PositronDeviceRapi]::LaunchProcess($image,$null,('--selftest-runtime-sql "'+$remote+'\fixture-two"'))
    $log=Join-Path $evidence 'two.log'
    $deadline=(Get-Date).AddSeconds(180)
    $captured=$null; $pagePass=$false
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
            $body=Get-Content $log -Raw -Encoding UTF8
            if ($body -match ('debug-session pid='+$appPid+'\b')) {
                $snapshot=[regex]::Match($body,('(?m)^positron pid='+$appPid+' tick=\d+ runtime-storage phase=open-end[^\r\n]*'))
                if ($snapshot.Success) {
                    $captured=$snapshot.Value
                    if ($captured -notmatch '\brc=0\b') { break }
                }
                if ($body -match 'runtime-sql PASS seed=two') { $pagePass=$true; break }
                if ($body -match 'runtime-sql FAIL|runtime-sql-failed') { break }
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    if (!$captured) { throw ('No storage snapshot; process not terminated: '+$appPid) }
    Wait-NormalExit 'two' $appPid
    $script:storageProbeEntries=0
    Copy-DataTree 'data' (Join-Path $evidence 'data-after') 0
    Hash-Tree (Join-Path $evidence 'data-after') (Join-Path $evidence 'data-after-sha256.txt')
    $crashAfter=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($item in $crashAfter.GetEnumerator()) { if (!$crashBefore.ContainsKey($item.Key) -or $crashBefore[$item.Key] -ne $item.Value) { throw 'New crash dump.' } }
    $status=if ($pagePass) { 'PAGE_PASS' } else { 'CAPTURED_FAILURE' }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),[string[]]@(
        ('status='+$status),('pid='+$appPid),('snapshot='+$captured),
        'original_binaries=11/11','diagnostic_exe=verified','normal_exit=zero_holders',
        'cold_backup=before_and_after','crash_check=PASS','isolation=NOT_RUN'))
    Write-Host $captured
    Write-Host ('RUNTIME STORAGE '+$status+' evidence='+$evidence)
} finally { if ($connected) { [PositronDeviceRapi]::Disconnect() } }
