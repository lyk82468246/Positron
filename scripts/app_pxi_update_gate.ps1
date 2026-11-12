# Two cold EXE runs against one isolated Notes installation.
param([Parameter(Mandatory=$true)][string]$RunRoot,[string]$Python='python')
$ErrorActionPreference='Stop'
if([Environment]::Is64BitProcess){throw 'Use 32-bit Windows PowerShell.'}
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$run=[IO.Path]::GetFullPath($RunRoot)
$runs=Join-Path $repo 'tmp\device-runs\'
if(!$run.StartsWith($runs,[StringComparison]::OrdinalIgnoreCase) -or
    $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-pxi-update$'){throw 'Require isolated update stage.'}
$audit=Get-Content (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$match=[regex]::Match($audit,'(?m)^remote_root=(\\Temp\\Positron-device-gate\\app-pxi-update-\d{8}-\d{6})\r?$')
if($audit -notmatch '(?m)^status=PASS\r?$' -or !$match.Success){throw 'Missing formal module audit.'}
$remote=$match.Groups[1].Value
$evidence=Join-Path $run 'app-pxi-update'
if(Test-Path -LiteralPath $evidence){throw 'Use a fresh isolated package.'}
[void][IO.Directory]::CreateDirectory($evidence)
$gatePath=Join-Path $PSScriptRoot 'device_gate.ps1'
$source=[regex]::Match((Get-Content $gatePath -Raw -Encoding UTF8),'(?s)\$rapiSource = @''\r?\n(.*?)\r?\n''@')
if(!$source.Success){throw 'Missing formal RAPI helper.'}
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
function Run-Update([string]$tag,[int]$mode){
    $previous=Join-Path $evidence ($tag+'-previous.log')
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$previous)
    [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
    $appPid=[PositronDeviceRapi]::LaunchProcess($remote+'\positron.exe',$null,('--selftest-pxi-update-'+$tag))
    $path=Join-Path $evidence ($tag+'.log')
    $deadline=(Get-Date).AddSeconds(120)
    do {
        if([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',$path)){
            $log=Get-Content $path -Raw -Encoding UTF8
            if($log -match ('debug-session pid='+$appPid+'\b') -and
                    $log -match ('pxi-update (PASS|FAIL) mode='+$mode+' phase=(\d+)')){
                $passed=$Matches[1] -eq 'PASS' -and $Matches[2] -eq '4'
                Wait-NormalExit $tag $appPid
                if(!$passed){throw ('Notes update fixture failed: '+$tag)}
                Assert-NoHolders $tag
                if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\positron.db',(Join-Path $evidence ($tag+'.db')))){
                    throw 'Missing cold registration evidence.'
                }
                return
            }
        }
        Start-Sleep -Milliseconds 250
    } while((Get-Date) -lt $deadline)
    throw ('Timed out waiting for fixture '+$tag+' PID '+$appPid)
}
$inputs=Join-Path $evidence 'inputs'
& $Python (Join-Path $PSScriptRoot 'generate_app_update_fixtures.py') --output $inputs
if($LASTEXITCODE -ne 0){throw 'Fixture generation failed.'}
[PositronDeviceRapi]::Connect()
try {
    Assert-NoHolders 'before'
    $before=[PositronDeviceRapi]::SnapshotCrashDumps()
    $bins=@(Get-ChildItem (Join-Path $run 'stage') -File|Where-Object{$_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll'})
    if($bins.Count -ne 12){throw 'Expected EXE plus eleven DLLs.'}
    foreach($bin in $bins){
        $back=Join-Path $evidence $bin.Name
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$bin.Name,$back) -or
                (Get-FileHash $back).Hash -ne (Get-FileHash $bin.FullName).Hash){throw ('Mixed binary '+$bin.Name)}
    }
    foreach($name in @('old.pxi','resources-v2.pxi')){
        $local=Join-Path $inputs $name
        $back=Join-Path $evidence $name
        [PositronDeviceRapi]::CopyFileToDevice($local,$remote+'\'+$name)
        if(![PositronDeviceRapi]::TryCopyFileFromDevice($remote+'\'+$name,$back) -or
                (Get-FileHash $back).Hash -ne (Get-FileHash $local).Hash){throw ('PXI mismatch '+$name)}
    }
    Run-Update 'prepare' 1
    Run-Update 'resume' 2
    Start-Sleep -Seconds 3
    $after=[PositronDeviceRapi]::SnapshotCrashDumps()
    foreach($item in $after.GetEnumerator()){
        if(!$before.ContainsKey($item.Key) -or $before[$item.Key] -ne $item.Value){throw 'New crash dump.'}
    }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'),@('status=PASS','binary_roundtrip=12/12',
        'normal_exit=zero_holders','crash_check=PASS','native_confirmation=fixture','notes_data=retained'))
} finally { [PositronDeviceRapi]::Disconnect() }
