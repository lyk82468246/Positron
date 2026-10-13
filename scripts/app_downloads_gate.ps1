param([Parameter(Mandatory=$true)][string]$RunRoot,[switch]$ConfirmedExclusiveWindow,[switch]$LeaveOpen)
$ErrorActionPreference='Stop'
if (!$ConfirmedExclusiveWindow -or [Environment]::Is64BitProcess) { throw 'Require exclusive device window and 32-bit PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=[IO.Path]::GetFullPath((Join-Path $repo 'tmp\device-runs'))+'\'
if (!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-downloads-[a-z0-9-]+$') { throw 'Require isolated formal download deployment.' }
$manifest=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($manifest,'(?m)^remote_root=([^\r\n]+)\r?$')
if ($manifest -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Require formal module audit.' }
$remote=$match.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-downloads-[a-z0-9-]+-\d{8}-\d{6}$') { throw 'Unexpected remote root.' }
$evidence=Join-Path $run 'app-downloads'
if (Test-Path $evidence) { throw 'Never overwrite evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$checks=[Collections.Generic.List[string]]::new()
$checks.Add('status=FAIL')
$connected=$false
function Write-Stage([string]$message) { Write-Host ('[app-downloads] '+$message) }
function Wait-DownloadExit([string]$mode,[uint32]$appPid) {
    $timer=[Diagnostics.Stopwatch]::StartNew()
    $attempt=0
    do {
        ++$attempt
        $auditLog=$remote+'\module-audit.log'
        $localLog=Join-Path $evidence ('audit-after-'+$mode+'-'+$attempt+'.log')
        [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
        try {
            [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') $auditLog $localLog 10)
            return
        } catch {
            if ($_.Exception.Message -notmatch 'did not confirm an empty holder set') { throw }
            $body=Get-Content $localLog -Raw -Encoding UTF8
            $summary=[regex]::Match($body,'(?m)^module_audit holders=(\d+) unavailable=0\r?$')
            $holders=[regex]::Matches($body,'(?m)^module_holder pid=(\d+) process=positron\.exe module=[^\r\n]+\r?$')
            if (!$summary.Success -or $holders.Count -eq 0 -or
                    $holders.Count -ne [int]$summary.Groups[1].Value -or
                    ([regex]::Matches($body,'(?m)^module_holder\b')).Count -ne $holders.Count -or
                    ([regex]::Matches($body,'(?m)^module_audit\b')).Count -ne 1) { throw }
            foreach($holder in $holders) { if ([uint32]$holder.Groups[1].Value -ne $appPid) { throw 'Other process prevents deployment.' } }
        }
        Start-Sleep -Milliseconds 200
    } while($timer.Elapsed.TotalSeconds -lt 30)
    throw ('Normal exit did not finish: '+$mode)
}
try {
    $gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
    $gate=Get-Content $gatePath -Raw -Encoding UTF8
    $source=[regex]::Match($gate,"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
    if (!$source.Success) { throw 'Missing formal RAPI helper.' }
    Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
    $tokens=$null; $errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($gatePath,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw 'Invalid formal gate.' }
    $fn=$ast.Find({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-RemoteModuleAudit'},$false)
    if ($null -eq $fn) { throw 'Missing module audit function.' }
    . ([scriptblock]::Create($fn.Extent.Text))
    [PositronDeviceRapi]::Connect(); $connected=$true
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $files=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object {$_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll'})
    if ($files.Count -ne 10) { throw 'Require EXE and nine DLLs.' }
    foreach($file in $files) {
        $local=Join-Path $evidence $file.Name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(($remote+'\'+$file.Name),$local) -or (Get-FileHash $local).Hash -ne (Get-FileHash $file.FullName).Hash) { throw ('Mixed package: '+$file.Name) }
    }
    $checks.Add('binary_roundtrip=10/10')
    foreach($mode in @('create','reopen','storage','services','ui')) {
        $auditLog=$remote+'\module-audit.log'
        [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
        [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') $auditLog (Join-Path $evidence ('audit-before-'+$mode+'.log')) 30)
        [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
        $argument=if ($mode -in @('create','reopen','ui')) { '--selftest-downloads-'+$mode } else { '--selftest-settings-'+$mode }
        $appPid=[PositronDeviceRapi]::LaunchProcess(($remote+'\positron.exe'),$null,$argument)
        $log=Join-Path $evidence ($mode+'.log')
        $pattern=if ($mode -in @('create','reopen')) {
            $number=if ($mode -eq 'create') {0} else {1}
            $stage=if ($mode -eq 'create') {6} else {8}
            '(?m)^positron pid='+$appPid+' tick=\d+ downloads-fixture PASS mode='+$number+' stage='+$stage+' large=2097152\r?$'
        } elseif ($mode -eq 'ui') {
            '(?m)^positron pid='+$appPid+' tick=\d+ downloads-ui selftest OK input=font authority=document history=single\r?$'
        } else { '(?m)^positron pid='+$appPid+' tick=\d+ settings-fixture PASS exit=0\r?$' }
        $timer=[Diagnostics.Stopwatch]::StartNew()
        $complete=$false
        do {
            if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
                $body=Get-Content $log -Raw -Encoding UTF8
                if ($body -match 'downloads-fixture FAIL|settings-fixture FAIL|selftest FAILED') { throw ('Fixture failed: '+$mode) }
                if (([regex]::Matches($body,$pattern)).Count -eq 1 -and $body -match ('debug-session pid='+$appPid+'\b')) {
                    if ($mode -eq 'create' -and ($body -notmatch 'download-prepare selftest OK stage=5' -or
                            $body -notmatch 'download-match selftest OK changed=refused received=0 published=0')) {
                        throw 'Missing application preflight or final-header regression.'
                    }
                    if ($mode -eq 'ui' -and ([regex]::Matches($body,
                            '(?m)^positron pid='+$appPid+' tick=\d+ positron record-fields selftest OK dpi-index=3 width-index=3 field=8\r?$')).Count -ne 1) {
                        throw 'Missing download record geometry regression.'
                    }
                    if ($mode -eq 'ui' -and $body -notmatch 'download-prepare-ui selftest OK async=responsive name=validated input=invalidated history=single') {
                        throw 'Missing real preparation UI regression.'
                    }
                    if ($mode -eq 'ui' -and $body -notmatch 'download-implicit-ui selftest OK automatic=once suffix=png failure=no_picker cancel=no_task stale=rejected') {
                        throw 'Missing implicit download/save continuation regression.'
                    }
                    $complete=$true; break
                }
            }
            Start-Sleep -Milliseconds 200
        } while($timer.Elapsed.TotalSeconds -lt 150)
        if (!$complete) { throw ('Missing completion: '+$mode) }
        # The fixture has returned only after normal HTTP/thread/DB drainage.
        Wait-DownloadExit $mode $appPid
        if ($mode -in @('create','reopen')) {
            if (![PositronDeviceRapi]::TryCopyFileFromDevice(($remote+'\positron.db'),(Join-Path $evidence ($mode+'.db')))) { throw 'Missing closed database.' }
        }
        $checks.Add($mode+'=PASS pid='+$appPid)
    }
    # Windows PowerShell 5.1 treats a BOM-less UTF-8 script as ANSI. Keep the
    # script ASCII and construct the exact UTF-16 device filename explicitly.
    $name='large-'+[char]0x4e2d+[char]0x6587+'.bin'
    $download=Join-Path $evidence $name
    if (![PositronDeviceRapi]::TryCopyFileFromDevice(($remote+'\'+$name),$download) -or (Get-Item $download).Length -ne 2097152) { throw 'Missing preserved large download.' }
    $expected=(& python -c "import hashlib; print(hashlib.sha256(b'D' * 2097152).hexdigest())").Trim()
    if ((Get-FileHash $download).Hash -ne $expected) { throw 'Downloaded byte hash mismatch.' }
    $checks.Add('large_file=2097152 exact_sha256_preserved_after_clear')
    if ($LeaveOpen) {
        $auditLog=$remote+'\module-audit.log'
        [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
        [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') $auditLog (Join-Path $evidence 'audit-before-ordinary.log') 30)
        [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
        $appPid=[PositronDeviceRapi]::LaunchProcess(($remote+'\positron.exe'),$null,'--url positron://downloads')
        $log=Join-Path $evidence 'ordinary-downloads.log'
        $timer=[Diagnostics.Stopwatch]::StartNew()
        $complete=$false
        do {
            if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$log)) {
                $body=Get-Content $log -Raw -Encoding UTF8
                if ($body -match 'selftest FAILED|downloads-fixture|downloads-ui selftest') { throw 'Ordinary startup polluted by a fixture.' }
                if ($body -match ('debug-session pid='+$appPid+'\b') -and
                        $body -match 'startup-window visible=1 foreground=1 page_visible=1' -and
                        $body -match 'internal-page commit url=positron://downloads kind=\d+ history=1' -and
                        $body -match 'settings-result request=\d+ op=7 result=0') { $complete=$true; break }
            }
            Start-Sleep -Milliseconds 200
        } while ($timer.Elapsed.TotalSeconds -lt 60)
        if (!$complete) { throw 'Ordinary download page/read/window did not complete.' }
        $checks.Add('ordinary=PASS pid='+$appPid)
        $checks.Add('picker_touch_sip_rotation_https=pending_manual')
    }
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach($name in $after.Keys) { if (!$before.ContainsKey($name) -or $before[$name] -ne $after[$name]) { throw ('New/changed crash dump: '+$name) } }
    $checks.Add('crash_check=PASS')
    $checks.Add('database=preserved')
    $checks[0]='status=PASS'
} catch {
    $checks.Add('failure='+$_.Exception.Message)
    Write-Host $_.Exception.Message
} finally {
    if($connected) { [PositronDeviceRapi]::Disconnect() }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),$checks)
}
if ($checks[0] -ne 'status=PASS') { exit 1 }
Write-Stage ('PASS evidence='+$evidence)
