# Real Notes UI/SDK/worker/DB acceptance in a fresh formal package.
param([Parameter(Mandatory=$true)][string]$RunRoot, [switch]$OpenManual)
$ErrorActionPreference='Stop'
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-sql$') {
    throw 'Require a fresh formal Debug app-runtime-sql package.'
}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing formal module audit.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-runtime-sql-\d{8}-\d{6}$') {
    throw 'Require an isolated SD package.'
}
$evidence=Join-Path $run 'app-runtime-notes'
if (Test-Path $evidence) { throw 'Never overwrite evidence or retry a partly seeded package.' }
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$source=[regex]::Match((Get-Content $gatePath -Raw -Encoding UTF8),"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if (!$source.Success) { throw 'Missing formal RAPI helper.' }
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
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
function Copy-Verified([string]$local,[string]$target,[string]$tag) {
    [PositronDeviceRapi]::CopyFileToDevice($local,$target)
    $back=Join-Path $evidence ($tag+'-'+[IO.Path]::GetFileName($local))
    if (![PositronDeviceRapi]::TryCopyFileFromDevice($target,$back) -or
            (Get-FileHash $local).Hash -ne (Get-FileHash $back).Hash) { throw ('Readback mismatch: '+$tag) }
}
function Replace-TestFile([string]$local,[string]$target,[string]$previous,[string]$tag) {
    $allowed=@(($remote+'\notes-one\acceptance-seed.js'),($remote+'\notes-two\acceptance-seed.js'),
        ($remote+'\notes-one\index.html'),($remote+'\notes-two\index.html'))
    if ($allowed -notcontains $target) { throw 'Only the gate-owned test entry/config may be replaced.' }
    $back=Join-Path $evidence ($tag+'-before.bin')
    if (![PositronDeviceRapi]::TryCopyFileFromDevice($target,$back) -or
            (Get-FileHash $back).Hash -ne (Get-FileHash $previous).Hash) { throw 'Previous test input changed; preserve it.' }
    # RAPI's atomic CeMoveFile publication cannot overwrite an existing file.
    # Callers have audited normal exit; preserve/verify this exact owned input.
    [PositronDeviceRapi]::DeleteFileIfExists($target)
    Copy-Verified $local $target $tag
}
$connected=$false
try {
    [PositronDeviceRapi]::Connect(); $connected=$true
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $space=[PositronDeviceRapi]::QueryVolumeStorage($remote)
    $internal=[PositronDeviceRapi]::QueryObjectStoreStorage()
    @($space,$internal) | ConvertTo-Json | Set-Content (Join-Path $evidence 'space.json') -Encoding UTF8
    if (!$space.Available -or $space.FreeBytes -lt 1048576 -or
            !$internal.Available -or $internal.FreeBytes -lt 65536) { throw 'Storage preflight failed.' }
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object { $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    if ($bins.Count -ne 11) { throw 'Expected EXE and ten DLLs.' }
    foreach ($bin in $bins) {
        $back=Join-Path $evidence $bin.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$back) -or
                (Get-FileHash $back).Hash -ne (Get-FileHash $bin.FullName).Hash) { throw ('Mixed binary '+$bin.Name) }
    }
    if (![Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $evidence 'positron.exe'))).Contains('runtime-notes PASS')) { throw 'Require the matching Debug Notes diagnostic EXE.' }
    $existing=@([PositronDeviceRapi]::ListSubdirectories($remote))
    if ($existing -contains 'notes-one' -or $existing -contains 'notes-two') { throw 'Notes roots must be fresh; do not erase them.' }
    $example=Join-Path $repo 'positron_app\examples\notes'
    $fixture=Join-Path $repo 'positron_app\fixtures\runtime-notes\acceptance.js'
    $utf8=New-Object Text.UTF8Encoding($false)
    $index=Get-Content (Join-Path $example 'index.html') -Raw -Encoding UTF8
    if (($index | Select-String -Pattern '</body>' -AllMatches).Matches.Count -ne 1) { throw 'Expected one Notes body.' }
    $testIndex=Join-Path $evidence 'acceptance-index.html'
    [IO.File]::WriteAllText($testIndex,$index.Replace('</body>',
        '<p id="acceptance-status">notes-waiting</p><script src="acceptance-seed.js"></script><script src="acceptance.js"></script></body>'),$utf8)
    foreach ($label in @('one','two')) {
        $root=$remote+'\notes-'+$label
        [PositronDeviceRapi]::EnsureDirectory($root)
        [PositronDeviceRapi]::EnsureDirectory($root+'\schema')
        foreach ($file in Get-ChildItem $example -File -Recurse | Where-Object { $_.Extension -ne '.md' }) {
            $relative=$file.FullName.Substring($example.Length+1)
            $inputPath=if ($relative -eq 'index.html') { $testIndex } else { $file.FullName }
            Copy-Verified $inputPath ($root+'\'+$relative) ($label+'-'+$file.Name)
        }
        Copy-Verified $fixture ($root+'\acceptance.js') $label
    }
    foreach ($round in @(1,2)) {
        foreach ($label in @('one','two')) {
            $tag=$label+'-'+$round
            Write-Host ('Running real Notes '+$tag)
            Assert-NoHolders ($tag+'-before')
            $seed=Join-Path $evidence ($tag+'-seed.js')
            [IO.File]::WriteAllText($seed,('window.notesAcceptanceSeed="'+$label+'";window.notesAcceptanceRound='+$round+';'),$utf8)
            if ($round -eq 1) {
                Copy-Verified $seed ($remote+'\notes-'+$label+'\acceptance-seed.js') $tag
            } else {
                Replace-TestFile $seed ($remote+'\notes-'+$label+'\acceptance-seed.js') (Join-Path $evidence ($label+'-1-seed.js')) $tag
            }
            [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',(Join-Path $evidence ($tag+'-previous.log')))
            [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
            [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-notes-result.log',(Join-Path $evidence ($tag+'-previous-result.log')))
            [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-notes-result.log')
            $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,
                ('--selftest-runtime-notes "'+$remote+'\notes-'+$label+'"'))
            $log=Join-Path $evidence ($tag+'.log')
            $proofLog=Join-Path $evidence ($tag+'-result.log')
            $deadline=(Get-Date).AddSeconds(180)
            $pass=$false
            do {
                if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-notes-result.log',$proofLog)) {
                    $body=Get-Content $proofLog -Raw -Encoding UTF8
                    if ($body -match ('positron pid='+$appPid+'\b')) {
                        if ($body -match 'runtime-notes FAIL|runtime-arguments FAIL|selftest FAILED') {
                            Wait-NormalExit $tag $appPid
                            [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)
                            throw ('Notes failed: '+$tag)
                        }
                        $proof='runtime-notes PASS seed='+$label+' rows=26 ui=notes'
                        if ($body -match ('positron pid='+$appPid+' tick=\d+ '+[regex]::Escape($proof))) { $pass=$true; break }
                    }
                }
                Start-Sleep -Milliseconds 250
            } while ((Get-Date) -lt $deadline)
            if (!$pass) { throw ('Notes timed out; process not terminated: '+$tag) }
            Wait-NormalExit $tag $appPid
            if (![PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) { throw 'Missing full Debug log.' }
            $full=Get-Content $log -Raw -Encoding UTF8
            if ($full -notmatch ('debug-session pid='+$appPid+'\b') -or
                    $full -notmatch 'runtime-arguments PASS checks=8' -or
                    $full -notmatch 'runtime-storage phase=open-end .*rc=0 error_result=0') { throw 'Missing startup evidence.' }
        }
    }
    # Normal module detach alone does not exclude a late crash dump writer.
    Start-Sleep -Seconds 3
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($item in $after.GetEnumerator()) { if (!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value) { throw 'New crash dump.' } }
    # Restore the actual example entry before the human gate. The identity and
    # seeded test database stay put; diagnostic JS is no longer referenced.
    foreach ($label in @('one','two')) {
        Replace-TestFile (Join-Path $example 'index.html') ($remote+'\notes-'+$label+'\index.html') $testIndex ('manual-'+$label)
    }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS',
        'binary_roundtrip=11/11','processes=4','sequence=one,two,one,two','rows_per_root=26',
        'same_id_roots=isolated','ui=real-notes-handlers','crud=PASS','search=title-and-body',
        'paging=four-pages-and-previous','reopen=persistent','normal_exit=zero_holders',
        'crash_check=PASS','user_data=untouched','manual_ui=NOT_RUN','manual_entry=restored-example'))
    Write-Host ('APP RUNTIME NOTES PASS evidence='+$evidence)
    if ($OpenManual) {
        $manualPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,
            ('--app-dir "'+$remote+'\notes-one"'))
        [IO.File]::WriteAllText((Join-Path $evidence 'manual.txt'),('pid='+$manualPid+"`r`nroot="+$remote+'\notes-one'))
        Write-Host ('Manual Notes opened; permission confirmation required, pid='+$manualPid)
    }
} finally { if ($connected) { [PositronDeviceRapi]::Disconnect() } }
