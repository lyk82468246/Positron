$ErrorActionPreference = 'Stop'
# Load only the real validators. No top-level gate, RAPI, build or device call.
$gateTokens = $null
$gateErrors = $null
$gateAst = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'db_file_gate.ps1'), [ref]$gateTokens, [ref]$gateErrors)
if ($gateErrors.Count) { throw $gateErrors[0] }
foreach ($name in @('Assert-ProbeLog', 'Assert-ChildLog')) {
    $definition = $gateAst.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name
    }, $false)
    if ($null -eq $definition) { throw "Missing validator: $name" }
    . ([scriptblock]::Create($definition.Extent.Text))
}
$package = '\Storage Card\Temp\Positron-device-gate\db-file-offline-20261004-000000'
$exe = $package + '\positron_db_file_probe.exe'
$fixture = $package + '\fixture-10-1234'
$parentLog = (@(
    "process pid=10 path=$exe",
    'fixture_storage=sd',
    "fixture_root=$fixture",
    'child role=create pid=20 created=1',
    'child role=create pid=20 wait=0 exit=0 completed=1',
    'child role=read pid=30 created=1',
    'child role=read pid=30 wait=0 exit=0 completed=1',
    'child role=verify-b pid=40 created=1',
    'child role=verify-b pid=40 wait=0 exit=0 completed=1',
    'fresh_processes=3 creator_pid=20 reader_pid=30 verifier_pid=40 creator_exited_before_reader=1 reader_exited_before_verifier=1',
    'DB_FILE_PROBE PASS'
) -join "`r`n") + "`r`n"
$dbPath = $fixture + '\' + ([string][char]0x4e2d) + [char]0x6587 + [char]0x76ee + [char]0x5f55 +
    '\' + ([string][char]0x6570) + [char]0x636e + [char]0x5e93 + '.sqlite'
$childLog = (@(
    "process pid=20 path=$exe",
    "db_module_path=$package\positron_db.dll",
    "fixture_db_utf8=$dbPath",
    'file_identity_guard=CREATE_NEW',
    'migration_script_failure=atomic',
    'exact_utf8_text=PASS exact_blob=PASS schema=PASS integrity=ok transaction_idle=1 statements=0',
    'DB_FILE_CHILD PASS closed_before_exit=1'
) -join "`r`n") + "`r`n"
$caseCount = 0
function Test-Validation([string] $Name, [scriptblock] $Action, [bool] $ShouldPass) {
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    if ($failed -eq $ShouldPass) { throw "Unexpected validator outcome: $Name" }
    $script:caseCount++
}
Test-Validation 'complete SD sequence' { Assert-ProbeLog $parentLog 10 'sd' $package } $true
$internalLog = $parentLog.Replace('fixture_storage=sd', 'fixture_storage=internal').Replace(
    "fixture_root=$fixture", 'fixture_root=\Temp\Positron-device-gate\db-file-fixtures\fixture-10-1234')
Test-Validation 'complete internal sequence' { Assert-ProbeLog $internalLog 10 'internal' $package } $true
Test-Validation 'partial sequence' { Assert-ProbeLog ($parentLog.Replace('DB_FILE_PROBE PASS', '')) 10 'sd' $package } $false
Test-Validation 'duplicate PASS' { Assert-ProbeLog ($parentLog + "DB_FILE_PROBE PASS`r`n") 10 'sd' $package } $false
Test-Validation 'explicit failure' { Assert-ProbeLog ($parentLog + "FAIL injected`r`n") 10 'sd' $package } $false
Test-Validation 'foreign parent' { Assert-ProbeLog $parentLog 11 'sd' $package } $false
Test-Validation 'foreign executable' { Assert-ProbeLog ($parentLog.Replace($exe, '\Windows\foreign.exe')) 10 'sd' $package } $false
Test-Validation 'duplicate storage' { Assert-ProbeLog ($parentLog + "fixture_storage=sd`r`n") 10 'sd' $package } $false
Test-Validation 'wrong storage' { Assert-ProbeLog $parentLog 10 'internal' $package } $false
Test-Validation 'wrong fixture owner' { Assert-ProbeLog ($parentLog.Replace('fixture-10-', 'fixture-11-')) 10 'sd' $package } $false
Test-Validation 'fixture traversal' { Assert-ProbeLog ($parentLog.Replace($fixture, ($fixture + '\..'))) 10 'sd' $package } $false
Test-Validation 'reused child PID' { Assert-ProbeLog ($parentLog.Replace('reader_pid=30', 'reader_pid=20')) 10 'sd' $package } $false
Test-Validation 'zero child PID' { Assert-ProbeLog ($parentLog.Replace('verifier_pid=40', 'verifier_pid=0')) 10 'sd' $package } $false
Test-Validation 'nonzero child exit' { Assert-ProbeLog ($parentLog.Replace('wait=0 exit=0', 'wait=0 exit=1')) 10 'sd' $package } $false
Test-Validation 'unwaited child' { Assert-ProbeLog ($parentLog.Replace('wait=0 exit=0', 'wait=258 exit=0')) 10 'sd' $package } $false
Test-Validation 'duplicate completion' { Assert-ProbeLog ($parentLog + "child role=read pid=30 wait=0 exit=0 completed=1`r`n") 10 'sd' $package } $false
$earlyReader = $parentLog.Replace("child role=read pid=30 created=1`r`n", '').Replace(
    'child role=create pid=20 wait=0 exit=0 completed=1',
    "child role=read pid=30 created=1`r`nchild role=create pid=20 wait=0 exit=0 completed=1")
Test-Validation 'reader before creator exit' { Assert-ProbeLog $earlyReader 10 'sd' $package } $false
Test-Validation 'exact child' { Assert-ChildLog $childLog 20 $package $dbPath 'create' } $true
Test-Validation 'read child' { Assert-ChildLog $childLog 20 $package $dbPath 'read' } $true
Test-Validation 'foreign child PID' { Assert-ChildLog $childLog 21 $package $dbPath 'create' } $false
Test-Validation 'wrong DB module' { Assert-ChildLog ($childLog.Replace('db_module_path=' + $package, 'db_module_path=\Windows')) 20 $package $dbPath 'create' } $false
Test-Validation 'wrong database file' { Assert-ChildLog ($childLog.Replace($dbPath, '\Temp\unrelated.sqlite')) 20 $package $dbPath 'create' } $false
Test-Validation 'missing migration rollback' { Assert-ChildLog ($childLog.Replace('migration_script_failure=atomic', '')) 20 $package $dbPath 'create' } $false
Test-Validation 'missing exclusive file reservation' { Assert-ChildLog ($childLog.Replace('file_identity_guard=CREATE_NEW', '')) 20 $package $dbPath 'create' } $false
Test-Validation 'bad integrity' { Assert-ChildLog ($childLog.Replace('integrity=ok', 'integrity=bad')) 20 $package $dbPath 'create' } $false
Test-Validation 'nonidle transaction' { Assert-ChildLog ($childLog.Replace('transaction_idle=1', 'transaction_idle=0')) 20 $package $dbPath 'create' } $false
Test-Validation 'live statement' { Assert-ChildLog ($childLog.Replace('statements=0', 'statements=1')) 20 $package $dbPath 'create' } $false
Test-Validation 'unclosed DB' { Assert-ChildLog ($childLog.Replace('closed_before_exit=1', 'closed_before_exit=0')) 20 $package $dbPath 'create' } $false
Test-Validation 'duplicate child PASS' { Assert-ChildLog ($childLog + "DB_FILE_CHILD PASS closed_before_exit=1`r`n") 20 $package $dbPath 'create' } $false
Test-Validation 'unknown child role' { Assert-ChildLog $childLog 20 $package $dbPath 'other' } $false

# No input may bind to PowerShell's read-only automatic $PID variable.
$pidAssignments = $gateAst.FindAll({ param($node)
    $node -is [Management.Automation.Language.AssignmentStatementAst] -and
        $node.Left.Extent.Text -ieq '$pid'
}, $true)
$pidParameters = $gateAst.FindAll({ param($node)
    $node -is [Management.Automation.Language.ParameterAst] -and
        $node.Name.VariablePath.UserPath -ieq 'pid'
}, $true)
if ($pidAssignments.Count -or $pidParameters.Count) { throw 'Gate assigns the automatic PID variable.' }
Write-Output ("DB file gate: {0} validator cases PASS (offline only; no WM6 acceptance)." -f $caseCount)
