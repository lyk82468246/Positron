# Actual EXE/SDK/worker/Runtime/DB integration; no build or forced termination.
param([Parameter(Mandatory=$true)][string]$RunRoot,
    [ValidatePattern('^app-runtime-sql(?:-[a-z0-9-]+)?$')]
    [string]$EvidenceName='app-runtime-sql',
    [ValidateSet('one','two')][string[]]$Roots=@('one','two'))
$ErrorActionPreference='Stop'
if ($Roots.Count -lt 1 -or $Roots.Count -gt 2 -or
        ($Roots | Select-Object -Unique).Count -ne $Roots.Count) { throw 'Select one or two distinct roots.' }
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-sql$') {
    throw 'Require a new formal app-runtime-sql deployment.'
}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing formal audit.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-runtime-sql-\d{8}-\d{6}$') {
    throw 'Require an isolated SD package.'
}
$evidence=Join-Path $run $EvidenceName
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
        Write-Host ('Verifying deployed binary '+$bin.Name)
        $copy=Join-Path $evidence $bin.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$copy) -or
                (Get-FileHash $copy).Hash -ne (Get-FileHash $bin.FullName).Hash) {
            throw ('Mixed binary '+$bin.Name)
        }
    }
    $fixture=Join-Path $repo 'positron_app\fixtures\runtime-sql'
    foreach ($label in $Roots) {
        $root=$remote+'\fixture-'+$label
        [PositronDeviceRapi]::EnsureDirectory($root)
        [PositronDeviceRapi]::EnsureDirectory($root+'\schema')
        foreach ($file in Get-ChildItem $fixture -File -Recurse | Where-Object { $_.Name -ne 'seed-two.js' }) {
            $relative=$file.FullName.Substring($fixture.Length+1)
            $inputPath=$file.FullName
            if ($label -eq 'two' -and $relative -eq 'seed.js') { $inputPath=Join-Path $fixture 'seed-two.js' }
            $copy=Join-Path $evidence ($label+'-fixture-'+$file.Name)
            if (![PositronDeviceRapi]::TryCopyFileFromDevice($root+'\'+$relative,$copy)) {
                [PositronDeviceRapi]::CopyFileToDevice($inputPath,$root+'\'+$relative)
                if (![PositronDeviceRapi]::TryCopyFileFromDevice($root+'\'+$relative,$copy)) { throw 'Fixture readback failed.' }
            }
            if ((Get-FileHash $copy).Hash -ne (Get-FileHash $inputPath).Hash) { throw 'Existing fixture mismatch; never overwrite.' }
        }
        foreach ($round in @(1,2)) {
            $tag=$label+'-'+$round
            Write-Host ('Running SQL author page '+$tag)
            Assert-NoHolders ($tag+'-before')
            [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',
                (Join-Path $evidence ($tag+'-previous.log')))
            [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
            $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,
                ('--selftest-runtime-sql "'+$root+'"'))
            $log=Join-Path $evidence ($tag+'.log')
            $deadline=(Get-Date).AddSeconds(180)
            $pass=$false
            do {
                if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
                    $body=Get-Content $log -Raw -Encoding UTF8
                    if ($body -match 'runtime-sql FAIL|runtime-sql-failed|selftest FAILED') { throw ('EXE SQL failed: '+$tag) }
                    $proof='runtime-sql PASS seed='+$label+' rows=32 typed=exact queue=bounded rollback=old-page revoke=isolated owner=closed'
                    if ($body -match ('debug-session pid='+$appPid+'\b') -and
                            $body -match ('positron pid='+$appPid+' tick=\d+ '+[regex]::Escape($proof))) {
                        $pass=$true; break
                    }
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (!$pass) { throw ('SQL page timed out; process not terminated: '+$tag) }
            Wait-NormalExit $tag $appPid
        }
    }
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($item in $after.GetEnumerator()) {
        if (!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value) { throw 'New crash dump.' }
    }
    $isolation=if ($Roots.Count -eq 2) { 'isolated' } else { 'NOT_RUN' }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS',
        'binary_roundtrip=11/11',('processes='+($Roots.Count*2)),('selected_roots='+($Roots -join ',')),
        'rows_per_root=32',('same_id_roots='+$isolation),
        'reopen=persistent','sdk=real-author-page','normal_exit=zero_holders',
        'crash_check=PASS','user_data=untouched','manual_ui=NOT_RUN'))
    Write-Host ('APP RUNTIME SQL PASS processes='+($Roots.Count*2)+' roots='+($Roots -join ',')+' isolation='+$isolation+' reopen=persistent normal_exit=zero')
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
