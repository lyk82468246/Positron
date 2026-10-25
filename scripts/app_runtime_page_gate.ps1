# Actual EXE candidate/resource composition. Does not build or terminate.
param([Parameter(Mandatory=$true)][string]$RunRoot)
$ErrorActionPreference='Stop'
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-page$') {
    throw 'Require a new formal app-runtime-page deployment.'
}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing formal audit.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-runtime-page-\d{8}-\d{6}$') {
    throw 'Require an isolated SD package.'
}
$evidence=Join-Path $run 'app-runtime-page'
if (Test-Path $evidence) { throw 'Never overwrite test evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$gate=Get-Content $gatePath -Raw -Encoding UTF8
$source=[regex]::Match($gate,"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$source.Success) { throw 'Missing formal RAPI helper.' }
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
function Import-Function([string]$path,[string]$name) {
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    $fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name},$false)
    if ($errors.Count -or $null -eq $fn) { throw ('Missing helper '+$name) }
    return $fn.Extent.Text
}
. ([scriptblock]::Create((Import-Function $gatePath 'Invoke-RemoteModuleAudit')))
. ([scriptblock]::Create((Import-Function (Join-Path $PSScriptRoot 'app_runtime_platform_gate.ps1') 'Wait-NormalExit')))
function Write-Stage([string]$message) { Write-Host $message }
function Assert-NoHolders([string]$tag) {
    [PositronDeviceRapi]::DeleteFileIfExists($remote+'\module-audit.log')
    [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') ($remote+'\module-audit.log') (Join-Path $evidence ($tag+'-modules.log')) 30)
}
$connected=$false
try {
    [PositronDeviceRapi]::Connect(); $connected=$true
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object {
        $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    if ($bins.Count -ne 11) { throw 'Expected EXE and ten DLLs.' }
    foreach ($bin in $bins) {
        $copy=Join-Path $evidence $bin.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$copy) -or
                (Get-FileHash $copy).Hash -ne (Get-FileHash $bin.FullName).Hash) {
            throw ('Mixed binary '+$bin.Name)
        }
    }
    $fixture=Join-Path $repo 'positron_app\fixtures\runtime-page'
    $root=$remote+'\fixture'
    foreach ($directory in @($root,($root+'\assets'),($root+'\pages'))) {
        [PositronDeviceRapi]::EnsureDirectory($directory)
    }
    foreach ($file in Get-ChildItem $fixture -File -Recurse) {
        $relative=$file.FullName.Substring($fixture.Length+1)
        [PositronDeviceRapi]::CopyFileToDevice($file.FullName,$root+'\'+$relative)
        $copy=Join-Path $evidence ('fixture-'+$file.Name)
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($root+'\'+$relative,$copy) -or
                (Get-FileHash $copy).Hash -ne (Get-FileHash $file.FullName).Hash) {
            throw 'Fixture hash mismatch.'
        }
    }
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',
        (Join-Path $evidence 'previous-debug.log'))
    [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
    $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,
        ('--selftest-runtime-app "'+$root+'"'))
    $log=Join-Path $evidence 'positron-debug.log'
    $deadline=(Get-Date).AddSeconds(180)
    $pass=$false
    do {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
            $body=Get-Content $log -Raw -Encoding UTF8
            if ($body -match 'runtime-page FAIL|runtime-arguments FAIL|selftest FAILED') { throw 'EXE directory page failed.' }
            if ($body -match ('debug-session pid='+$appPid+'\b') -and
                    $body -match 'runtime-arguments PASS checks=8' -and
                    $body -match ('positron pid='+$appPid+' tick=\d+ runtime-page PASS owner=worker assets=local scripts=ordered css=import svg=retained rollback=old-page newtab=denied services=none optional=fallback stale=isolated owner_close=released')) {
                $pass=$true; break
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $deadline)
    if (!$pass) { throw 'Directory page timed out; process not terminated.' }
    Wait-NormalExit 'page' $appPid
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($item in $after.GetEnumerator()) {
        if (!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value) { throw 'New crash dump.' }
    }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS',
        'binary_roundtrip=11/11','resources=real-exe-candidate','normal_exit=zero_holders',
        'crash_check=PASS','sql_sdk=NOT_CONNECTED','user_data=untouched'))
    Write-Host 'APP RUNTIME PAGE PASS binary_roundtrip=11/11 owner=worker normal_exit=zero sql_sdk=NOT_CONNECTED'
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
