# EXE-owned durable registry, Runtime recovery, then real installed Browser page.
param([Parameter(Mandatory=$true)][string]$RunRoot,[string]$Python='python')
$ErrorActionPreference='Stop'
if([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if(!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-runtime-install$') { throw 'Require a fresh formal installation package.' }
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success) { throw 'Missing formal audit.' }
$remote=$match.Groups[1].Value
if($remote -notmatch '^\\Temp\\Positron-device-gate\\app-runtime-install-\d{8}-\d{6}$') { throw 'Use an isolated internal Temp package; never relocate bound data.' }
$evidence=Join-Path $run 'app-runtime-install'
if(Test-Path $evidence) { throw 'Never overwrite evidence or reuse a partially installed package.' }
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$source=[regex]::Match((Get-Content $gatePath -Raw -Encoding UTF8),"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if(!$source.Success) { throw 'Missing formal RAPI helper.' }
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
function Import-Function([string]$path,[string]$name) {
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)
    if($errors.Count -or !$fn) { throw ('Missing helper '+$name) }
    return $fn.Extent.Text
}
. ([scriptblock]::Create((Import-Function $gatePath 'Invoke-RemoteModuleAudit')))
. ([scriptblock]::Create((Import-Function (Join-Path $PSScriptRoot 'app_runtime_platform_gate.ps1') 'Wait-NormalExit')))
function Write-Stage([string]$message) { Write-Host $message }
function Assert-NoHolders([string]$tag) {
    [PositronDeviceRapi]::DeleteFileIfExists($remote+'\module-audit.log')
    [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') ($remote+'\module-audit.log') (Join-Path $evidence ($tag+'-modules.log')) 30)
}
function Save-Utf8([string]$path,[string]$text) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
    [IO.File]::WriteAllText($path,$text,(New-Object Text.UTF8Encoding($false)))
}
$inputs=Join-Path $evidence 'inputs'
function Write-App([string]$name,[string]$id,[bool]$sql,[string[]]$migrations) {
    $directory=Join-Path $inputs $name
    $manifest=[ordered]@{manifestVersion=1;id=$id;name='Installation acceptance';version=1;runtimeApiVersion=1;entry='index.html';permissions=@();migrations=@()}
    if($sql) { $manifest.permissions=@('storage.sql') }
    for($n=0;$n -lt $migrations.Count;++$n) {
        $file='schema/'+($n+1)+'.sql'
        $manifest.migrations+=@{version=$n+1;file=$file}
        Save-Utf8 (Join-Path $directory $file) $migrations[$n]
    }
    Save-Utf8 (Join-Path $directory 'manifest.json') ($manifest | ConvertTo-Json -Compress -Depth 6)
    Save-Utf8 (Join-Path $directory 'index.html') '<!doctype html><html><head><title>Installed application</title></head><body><p>Installed resource view.</p></body></html>'
}
function Run-Case([string]$tag,[string]$arguments,[string]$proof,[int]$timeout) {
    Assert-NoHolders ($tag+'-before')
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',(Join-Path $evidence ($tag+'-previous.log')))
    [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
    $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,$arguments)
    $path=Join-Path $evidence ($tag+'.log')
    $deadline=(Get-Date).AddSeconds($timeout)
    $pass=$false
    do {
        if([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$path)) {
            $text=Get-Content $path -Raw -Encoding UTF8
            if($text -match ('debug-session pid='+$appPid+'\b')) {
                if($text -match 'runtime-install-native FAIL|runtime-sql FAIL|runtime-sql-failed|settings-fixture FAIL') {
                    Wait-NormalExit $tag $appPid
                    throw ('Installation acceptance failed: '+$tag)
                }
                if($text -match ('positron pid='+$appPid+' tick=\d+ '+[regex]::Escape($proof))) { $pass=$true;break }
            }
        }
        Start-Sleep -Milliseconds 250
    } while((Get-Date) -lt $deadline)
    if(!$pass) { throw ('Timed out; process not terminated: '+$tag) }
    Wait-NormalExit $tag $appPid
    Write-Host ('INSTALL CASE PASS '+$tag+' pid='+$appPid)
}
$connected=$false
try {
    [PositronDeviceRapi]::Connect();$connected=$true
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $space=[PositronDeviceRapi]::QueryVolumeStorage($remote)
    $internal=[PositronDeviceRapi]::QueryObjectStoreStorage()
    @($space,$internal) | ConvertTo-Json | Set-Content (Join-Path $evidence 'space.json') -Encoding UTF8
    if(!$space.Available -or $space.FreeBytes -lt 5*1024*1024 -or !$internal.Available -or $internal.FreeBytes -lt 5*1024*1024) { throw 'Require space for the private 4 MiB boundary corpus.' }
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File | Where-Object {$_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll'})
    if($bins.Count -ne 12) { throw 'Expected EXE plus eleven current DLLs.' }
    foreach($bin in $bins) {
        $back=Join-Path $evidence $bin.Name
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$back) -or (Get-FileHash $back).Hash -ne (Get-FileHash $bin.FullName).Hash) { throw ('Mixed binary '+$bin.Name) }
    }
    $existing=@([PositronDeviceRapi]::ListSubdirectories($remote))
    if($existing -contains 'data' -or $existing -contains 'no-sql') { throw 'Require a fresh installation root.' }
    Write-App 'no-sql' 'org.positron.nosql' $false @()
    Write-App 'schema-zero' 'org.positron.zero' $true @()
    $marker='CREATE TABLE marker(id INTEGER PRIMARY KEY);INSERT INTO marker VALUES(1);'
    Write-App 'lost' 'org.positron.lost' $true @($marker)
    Write-App 'unresolved' 'org.positron.unresolved' $true @($marker)
    Write-App 'cancel' 'org.positron.cancel' $true @($marker)
    Write-App 'prefix' 'org.positron.prefix' $true @(
        'CREATE TABLE marker(id INTEGER PRIMARY KEY);INSERT INTO marker VALUES(1),(2),(3),(4),(5),(6),(7),(8);',
        'CREATE TABLE bulk(a INTEGER,b INTEGER,c INTEGER,d INTEGER);INSERT INTO bulk SELECT a.id,b.id,c.id,d.id FROM marker a CROSS JOIN marker b CROSS JOIN marker c CROSS JOIN marker d;')
    Write-App 'snapshot' 'org.positron.snapshot' $false @()
    Write-App 'capture' 'org.positron.capture' $false @()
    Write-App 'bounds' 'org.positron.bounds' $false @()
    for($n=0;$n -lt 125;++$n) { Save-Utf8 (Join-Path $inputs ('bounds\empty-'+$n+'.bin')) '' }
    $sql=Join-Path $inputs 'sql'
    [void][IO.Directory]::CreateDirectory($sql)
    Copy-Item -Path (Join-Path $repo 'positron_app\fixtures\runtime-sql\*') -Destination $sql -Recurse
    # seed-two is unreferenced but included in the verified complete view.
    foreach($directory in Get-ChildItem $inputs -Directory -Recurse) {
        [PositronDeviceRapi]::EnsureDirectory($remote+'\'+$directory.FullName.Substring($inputs.Length+1))
    }
    foreach($file in Get-ChildItem $inputs -File -Recurse) {
        $relative=$file.FullName.Substring($inputs.Length+1)
        [PositronDeviceRapi]::CopyFileToDevice($file.FullName,$remote+'\'+$relative)
        $back=Join-Path $evidence ('input-'+$relative.Replace('\','_'))
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$relative,$back) -or (Get-FileHash $back).Hash -ne (Get-FileHash $file.FullName).Hash) { throw 'Fixture readback failed.' }
    }
    Run-Case 'native' '--selftest-installation-native' 'runtime-install-native PASS phase=10 registry=persistent cas=exact recovery=explicit snapshot=immutable' 300
    $native=Get-Content (Join-Path $evidence 'native.log') -Raw -Encoding UTF8
    $wrong=[regex]::Match($native,'wrong-data source=([^\r\n]+) target=([^\r\n]+)')
    if(!$wrong.Success) { throw 'Missing wrong-data evidence.' }
    foreach($name in @('source','target')) {
        $index=if($name -eq 'source'){1}else{2}
        $path=$wrong.Groups[$index].Value
        if(!$path.StartsWith($remote+'\data\v1\installed\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected diagnostic DB path.' }
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($path,(Join-Path $evidence ('wrong-'+$name+'.db')))) { throw 'Missing cold database readback.' }
    }
    if((Get-FileHash (Join-Path $evidence 'wrong-source.db')).Hash -ne (Get-FileHash (Join-Path $evidence 'wrong-target.db')).Hash) { throw 'Rejected wrong identity changed file bytes.' }
    foreach($round in @(1,2)) {
        Run-Case ('installed-'+$round) '--selftest-runtime-installed org.positron.sql_demo' 'runtime-sql PASS seed=one rows=32 typed=exact queue=bounded rollback=old-page revoke=isolated owner=closed' 180
    }
    Run-Case 'settings-storage' '--selftest-settings-storage' 'settings-fixture PASS exit=0' 120
    Run-Case 'settings-services' '--selftest-settings-services' 'settings-fixture PASS exit=0' 120
    Start-Sleep -Seconds 3
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach($item in $after.GetEnumerator()) { if(!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value) { throw 'New crash dump.' } }
    if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\positron.db',(Join-Path $evidence 'registry-cold.db'))) { throw 'Missing cold registration database.' }
    & $Python (Join-Path $PSScriptRoot 'test_app_install_registry_evidence.py') --run-root $run
    if($LASTEXITCODE -ne 0) { throw 'Independent cold registry validation failed.' }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS','binary_roundtrip=12/12',
        'registry=real-exe-main-db','native=phase10','installed_pages=2-processes','wrong_data=unchanged',
        'recovery=explicit','snapshot=complete-immutable','normal_exit=zero_holders','crash_check=PASS','manual_ui=NOT_RUN','user_data=untouched'))
    Write-Host ('APP RUNTIME INSTALL PASS evidence='+$evidence)
} finally { if($connected) { [PositronDeviceRapi]::Disconnect() } }
