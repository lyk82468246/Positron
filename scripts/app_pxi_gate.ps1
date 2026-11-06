# Actual EXE PXI output, durable registration, then installed Browser/SDK.
param([Parameter(Mandatory=$true)][string]$RunRoot,[string]$Python='python')
$ErrorActionPreference='Stop'
if([Environment]::Is64BitProcess){throw 'Use 32-bit Windows PowerShell.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if(!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
    $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-pxi$'){throw 'Require fresh formal PXI package.'}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=([^\r\n]+)\r?$')
if($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success){throw 'Missing formal audit.'}
$remote=$match.Groups[1].Value
if($remote -notmatch '^\\Temp\\Positron-device-gate\\app-pxi-\d{8}-\d{6}$'){throw 'Explicit isolated internal Temp only.'}
$evidence=Join-Path $run 'app-pxi'
if(Test-Path $evidence){throw 'Never overwrite evidence or reuse a partial package.'}
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$source=[regex]::Match((Get-Content $gatePath -Raw -Encoding UTF8),"(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
if(!$source.Success){throw 'Missing formal RAPI helper'}
Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
function Import-Function([string]$path,[string]$name){
    $tokens=$null;$errors=$null
    $ast=[Management.Automation.Language.Parser]::ParseFile($path,[ref]$tokens,[ref]$errors)
    $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$false)
    if($errors.Count -or !$fn){throw ('Missing helper '+$name)}
    return $fn.Extent.Text
}
. ([scriptblock]::Create((Import-Function $gatePath 'Invoke-RemoteModuleAudit')))
. ([scriptblock]::Create((Import-Function (Join-Path $PSScriptRoot 'app_runtime_platform_gate.ps1') 'Wait-NormalExit')))
function Write-Stage([string]$message){Write-Host $message}
function Assert-NoHolders([string]$tag){
    [PositronDeviceRapi]::DeleteFileIfExists($remote+'\module-audit.log')
    [void](Invoke-RemoteModuleAudit ($remote+'\positron_process_cleanup.exe') ($remote+'\module-audit.log') (Join-Path $evidence ($tag+'-modules.log')) 30)
}
function Run-Case([string]$tag,[string]$arguments,[string]$proof,[int]$timeout){
    Assert-NoHolders ($tag+'-before')
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',(Join-Path $evidence ($tag+'-previous.log')))
    [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
    $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,$arguments)
    $path=Join-Path $evidence ($tag+'.log');$deadline=(Get-Date).AddSeconds($timeout);$pass=$false
    do{
        if([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$path)){
            $text=Get-Content $path -Raw -Encoding UTF8
            if($text -match ('debug-session pid='+$appPid+'\b')){
                if($text -match 'pxi-arguments FAIL|pxi-native FAIL|runtime-sql FAIL|runtime-sql-failed|settings-fixture FAIL'){
                    Wait-NormalExit $tag $appPid;throw ('PXI case failed '+$tag)
                }
                if($text -match ('positron pid='+$appPid+' tick=\d+ '+[regex]::Escape($proof))){$pass=$true;break}
            }
        }
        Start-Sleep -Milliseconds 250
    }while((Get-Date) -lt $deadline)
    if(!$pass){throw ('Timed out; process retained '+$tag)}
    Wait-NormalExit $tag $appPid
    Write-Host ('PXI CASE PASS '+$tag+' pid='+$appPid)
}
$inputs=Join-Path $evidence 'inputs'
& $Python (Join-Path $PSScriptRoot 'generate_app_pxi_fixtures.py') --output $inputs
if($LASTEXITCODE -ne 0){throw 'Fixture generation failed'}
$connected=$false
try{
    [PositronDeviceRapi]::Connect();$connected=$true
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $space=[PositronDeviceRapi]::QueryVolumeStorage($remote)
    $internal=[PositronDeviceRapi]::QueryObjectStoreStorage()
    @($space,$internal)|ConvertTo-Json|Set-Content (Join-Path $evidence 'space.json') -Encoding UTF8
    if(!$space.Available -or $space.FreeBytes -lt 5*1024*1024 -or !$internal.Available -or $internal.FreeBytes -lt 5*1024*1024){throw 'Need room for exact 4MiB output test.'}
    if(@([PositronDeviceRapi]::ListSubdirectories($remote)) -contains 'apps'){throw 'Require fresh output base.'}
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File|Where-Object{$_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll'})
    if($bins.Count -ne 12){throw 'Expected EXE plus eleven DLLs'}
    foreach($bin in $bins){
        $back=Join-Path $evidence $bin.Name
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$back) -or (Get-FileHash $back).Hash -ne (Get-FileHash $bin.FullName).Hash){throw ('Mixed binary '+$bin.Name)}
    }
    foreach($file in Get-ChildItem $inputs -File){
        [PositronDeviceRapi]::CopyFileToDevice($file.FullName,$remote+'\'+$file.Name)
        $back=Join-Path $evidence $file.Name
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$file.Name,$back) -or (Get-FileHash $back).Hash -ne (Get-FileHash $file.FullName).Hash){throw 'PXI input mismatch'}
    }
    Run-Case 'native' '--selftest-pxi-native' 'pxi-native PASS phase=10 crc=complete cleanup=owned retention=conservative' 300
    foreach($round in @(1,2)){
        Run-Case ('installed-'+$round) '--selftest-runtime-installed org.positron.sql_demo' 'runtime-sql PASS seed=one rows=32 typed=exact queue=bounded rollback=old-page revoke=isolated owner=closed' 180
    }
    Run-Case 'settings-storage' '--selftest-settings-storage' 'settings-fixture PASS exit=0' 120
    if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\positron.db',(Join-Path $evidence 'registry-cold.db'))){throw 'Missing cold registry'}
    & $Python (Join-Path $PSScriptRoot 'verify_app_pxi_output.py') --run-root $run --emit-manifest
    if($LASTEXITCODE -ne 0){throw 'Invalid registration manifest'}
    $files=Get-Content (Join-Path $evidence 'output-manifest.json') -Raw -Encoding UTF8|ConvertFrom-Json
    foreach($file in $files){
        if(!$file.remote.StartsWith($remote+'\apps\pxi-',[StringComparison]::OrdinalIgnoreCase)){throw 'Output outside dedicated apps root'}
        $local=Join-Path $evidence $file.local
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($local))
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($file.remote,$local)){throw 'Missing cold extracted resource'}
    }
    & $Python (Join-Path $PSScriptRoot 'verify_app_pxi_output.py') --run-root $run
    if($LASTEXITCODE -ne 0){throw 'Independent output validation failed'}
    Start-Sleep -Seconds 3
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach($item in $after.GetEnumerator()){if(!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value){throw 'New crash dump'}}
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS','binary_roundtrip=12/12','native=phase10',
        'installed_pages=2-processes','output=independently-verified','normal_exit=zero_holders','crash_check=PASS','manual_ui=NOT_RUN','user_data=untouched'))
    Write-Host ('APP PXI PASS evidence='+$evidence)
}finally{if($connected){[PositronDeviceRapi]::Disconnect()}}
