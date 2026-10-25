# Explicit native provider conformance, not author-page/UI acceptance.
param([Parameter(Mandatory=$true)][string]$RunRoot,
    [ValidatePattern('^app-runtime-platform(?:-[a-z0-9-]+)?$')]
    [string]$EvidenceName='app-runtime-platform')
$ErrorActionPreference='Stop'
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-platform$') {
    throw 'Require a dedicated formal app-runtime-platform run.'
}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing formal audit.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-runtime-platform-\d{8}-\d{6}$') {
    throw 'Require a new isolated SD deployment.'
}
$evidence=Join-Path $run $EvidenceName
if (Test-Path $evidence) { throw 'Never overwrite evidence or fixture data.' }
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$gate=Get-Content $gatePath -Raw -Encoding UTF8
$source=[regex]::Match($gate,"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$source.Success) { throw 'Missing formal RAPI helper.' }
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($gatePath,[ref]$tokens,[ref]$errors)
$fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Invoke-RemoteModuleAudit'},$false)
if ($errors.Count -or $null -eq $fn) { throw 'Invalid formal audit helper.' }
. ([scriptblock]::Create($fn.Extent.Text))
function Write-Stage([string]$message) { Write-Host $message }
function Assert-NoHolders([string]$tag) {
    [PositronDeviceRapi]::DeleteFileIfExists($remote+'\module-audit.log')
    [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') ($remote+'\module-audit.log') (Join-Path $evidence ($tag+'-modules.log')) 30)
}
function Wait-NormalExit([string]$tag,[uint32]$expectedPid) {
    $deadline=(Get-Date).AddSeconds(30)
    $attempt=0
    do {
        ++$attempt
        $name=$tag+'-exit-'+$attempt
        try { Assert-NoHolders $name; return } catch {
            $path=Join-Path $evidence ($name+'-modules.log')
            if (!(Test-Path $path)) { throw }
            $body=Get-Content $path -Raw -Encoding UTF8
            $holders=[regex]::Matches($body,'(?m)^module_holder pid=(\d+)\b')
            if ($body -notmatch 'module_audit holders=\d+ unavailable=0' -or
                    !$holders.Count) { throw }
            foreach ($holder in $holders) {
                if ($holder.Groups[1].Value -ne [string]$expectedPid) { throw }
            }
            # A PASS log precedes CRT/DLL detach. Wait only for this exact
            # test process to exit normally; another holder is never accepted.
        }
        Start-Sleep -Milliseconds 1000
    } while ((Get-Date) -lt $deadline)
    throw 'Test logged PASS but did not release DLLs within 30 seconds.'
}
$connected=$false
try {
    [PositronDeviceRapi]::Connect(); $connected=$true
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',
        (Join-Path $evidence 'previous-debug.log'))
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object {
        $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    if ($bins.Count -ne 11) { throw 'Expected EXE plus ten matching DLLs.' }
    foreach ($bin in $bins) {
        $copy=Join-Path $evidence $bin.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$copy) -or
                (Get-FileHash $copy).Hash -ne (Get-FileHash $bin.FullName).Hash) {
            throw ('Mixed binary: '+$bin.Name)
        }
    }
    $fixture=Join-Path $repo 'positron_app\fixtures\runtime-platform'
    foreach ($label in @('A','B')) {
        # Build Unicode explicitly; Windows PowerShell also runs with ACP
        # script decoding on some hosts. Both root and leaf must use wide I/O.
        $root=$remote+'\runtime-platform-'+[char]0x76ee+[char]0x5f55+'-'+$label
        [PositronDeviceRapi]::EnsureDirectory($root)
        [PositronDeviceRapi]::EnsureDirectory($root+'\schema')
        foreach ($file in Get-ChildItem $fixture -File -Recurse) {
            $relative=$file.FullName.Substring($fixture.Length+1)
            $copy=Join-Path $evidence ($label+'-'+$file.Name)
            if (![PositronDeviceRapi]::TryCopyFileFromDevice($root+'\'+$relative,$copy)) {
                [PositronDeviceRapi]::CopyFileToDevice($file.FullName,$root+'\'+$relative)
            }
            if (![PositronDeviceRapi]::TryCopyFileFromDevice($root+'\'+$relative,$copy) -or
                    (Get-FileHash $copy).Hash -ne (Get-FileHash $file.FullName).Hash) {
                throw 'Fixture roundtrip mismatch.'
            }
        }
        foreach ($round in @(1,2)) {
            Assert-NoHolders ($label+'-'+$round+'-before')
            [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
            $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,
                ('--selftest-runtime-platform "'+$root+'"'))
            $log=Join-Path $evidence ($label+'-'+$round+'.log')
            $deadline=(Get-Date).AddSeconds(120)
            $pass=$false
            do {
                if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
                    $body=Get-Content $log -Raw -Encoding UTF8
                    if ($body -match 'runtime-platform FAIL') { throw 'Provider fixture failed.' }
                    if ($body -match ('debug-session pid='+$appPid+'\b') -and
                            $body -match ('positron pid='+$appPid+' tick=\d+ runtime-platform PASS exit=0') -and
                            $body -match 'runtime-platform proof worker=1 opens=2 duplicate=busy pinned=1 unicode=1 traversal=denied grant=explicit sql=restricted reopen=exact') {
                        $pass=$true; break
                    }
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (!$pass) { throw 'Provider test timed out; process not terminated.' }
            Wait-NormalExit ($label+'-'+$round) $appPid
        }
    }
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($item in $after.GetEnumerator()) {
        if (!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value) { throw 'New crash dump.' }
    }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS',
        'processes=4','roots=2','binary_roundtrip=11/11','normal_exit=zero_holders',
        'unicode_root=PASS','unicode_resource=PASS',
        'crash_check=PASS','author_ui=NOT_RUN','user_data=untouched'))
    Write-Host 'APP RUNTIME PLATFORM PASS processes=4 roots=2 binary_roundtrip=11/11 author_ui=NOT_RUN'
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
}
