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

$lockParent = (@(
    "process pid=10 path=$exe", 'fixture_storage=sd', "fixture_root=$fixture",
    'lock_suite=rw-ww-v1',
    'child role=create pid=20 created=1', 'child role=create pid=20 wait=0 exit=0 completed=1',
    'child role=rw-reader pid=30 created=1', 'child role=rw-writer pid=40 created=1',
    'child role=rw-reader pid=30 wait=0 exit=0 completed=1',
    'child role=rw-writer pid=40 wait=0 exit=0 completed=1',
    'child role=ww-owner pid=50 created=1', 'child role=ww-contender pid=60 created=1',
    'child role=ww-owner pid=50 wait=0 exit=0 completed=1',
    'child role=ww-contender pid=60 wait=0 exit=0 completed=1',
    'child role=lock-verify pid=70 created=1',
    'child role=lock-verify pid=70 wait=0 exit=0 completed=1',
    'lock_processes=6 rw_pair=concurrent ww_pair=concurrent cold_verifier_after_all_exits=1',
    'DB_FILE_PROBE PASS'
) -join "`r`n") + "`r`n"
Test-Validation 'complete lock pairs' { Assert-ProbeLog $lockParent 10 'sd' $package 'Locks' } $true
Test-Validation 'lock suite not Unicode' { Assert-ProbeLog $lockParent 10 'sd' $package } $false
Test-Validation 'Unicode suite not locks' { Assert-ProbeLog $parentLog 10 'sd' $package 'Locks' } $false
Test-Validation 'lock missing suite identity' { Assert-ProbeLog ($lockParent.Replace('lock_suite=rw-ww-v1','')) 10 'sd' $package 'Locks' } $false
Test-Validation 'lock repeated PID' { Assert-ProbeLog ($lockParent.Replace('pid=60','pid=50')) 10 'sd' $package 'Locks' } $false
Test-Validation 'lock exit failure' { Assert-ProbeLog ($lockParent.Replace('wait=0 exit=0','wait=0 exit=1')) 10 'sd' $package 'Locks' } $false
$sequential = $lockParent.Replace("child role=rw-writer pid=40 created=1`r`n", '').Replace(
    'child role=rw-reader pid=30 wait=0 exit=0 completed=1',
    "child role=rw-reader pid=30 wait=0 exit=0 completed=1`r`nchild role=rw-writer pid=40 created=1")
Test-Validation 'sequential pair rejected' { Assert-ProbeLog $sequential 10 'sd' $package 'Locks' } $false
$earlyVerify = $lockParent.Replace("child role=lock-verify pid=70 created=1`r`n", '').Replace(
    'child role=ww-contender pid=60 wait=0 exit=0 completed=1',
    "child role=lock-verify pid=70 created=1`r`nchild role=ww-contender pid=60 wait=0 exit=0 completed=1")
Test-Validation 'lock verifier before competitor exit' { Assert-ProbeLog $earlyVerify 10 'sd' $package 'Locks' } $false
$lockBase = $childLog.Replace("file_identity_guard=CREATE_NEW`r`n", '').Replace(
    "migration_script_failure=atomic`r`n", '').Replace(
    'exact_utf8_text=PASS exact_blob=PASS schema=PASS integrity=ok transaction_idle=1 statements=0',
    'lock_integrity=ok transaction_idle=1 statements=0')
$rwLog = $lockBase + "lock_busy phase=rw-commit result=-4 category=5 native=5 extended=5 active=1 txn=2 statements=0 cleanup=0`r`nrw_commit_retry=PASS`r`n"
$wwLog = $lockBase + "lock_busy phase=ww-begin result=-4 category=5 native=5 extended=5 active=0 txn=0 statements=0 cleanup=0`r`nww_begin_retry=PASS`r`n"
Test-Validation 'exact RW BUSY' { Assert-ChildLog $rwLog 20 $package $dbPath 'rw-writer' } $true
Test-Validation 'exact WW BUSY' { Assert-ChildLog $wwLog 20 $package $dbPath 'ww-contender' } $true
Test-Validation 'old and new snapshots' { Assert-ChildLog ($lockBase + "rw_old_snapshot=PASS`r`nrw_new_snapshot=PASS`r`n") 20 $package $dbPath 'rw-reader' } $true
Test-Validation 'owner release' { Assert-ChildLog ($lockBase + "ww_owner_commit=PASS`r`n") 20 $package $dbPath 'ww-owner' } $true
Test-Validation 'cold lock verifier' { Assert-ChildLog ($lockBase + "locks_cold_reopen=PASS rows=4 schema=1`r`n") 20 $package $dbPath 'lock-verify' } $true
Test-Validation 'wrong native BUSY' { Assert-ChildLog ($rwLog.Replace('native=5','native=6')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'wrong extended BUSY' { Assert-ChildLog ($rwLog.Replace('extended=5','extended=6')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'wrong BUSY category' { Assert-ChildLog ($rwLog.Replace('category=5','category=1')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'commit BUSY inactive' { Assert-ChildLog ($rwLog.Replace('active=1','active=0')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'begin BUSY active' { Assert-ChildLog ($wwLog.Replace('active=0','active=1')) 20 $package $dbPath 'ww-contender' } $false
Test-Validation 'commit BUSY not write transaction' { Assert-ChildLog ($rwLog.Replace('txn=2','txn=1')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'missing commit retry' { Assert-ChildLog ($rwLog.Replace('rw_commit_retry=PASS','')) 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'duplicate commit retry' { Assert-ChildLog ($rwLog + "rw_commit_retry=PASS`r`n") 20 $package $dbPath 'rw-writer' } $false
Test-Validation 'missing new snapshot' { Assert-ChildLog ($lockBase + "rw_old_snapshot=PASS`r`n") 20 $package $dbPath 'rw-reader' } $false
Test-Validation 'cold verifier missing row' { Assert-ChildLog ($lockBase + "locks_cold_reopen=PASS rows=3 schema=1`r`n") 20 $package $dbPath 'lock-verify' } $false
Test-Validation 'unknown suite' { Assert-ProbeLog $lockParent 10 'sd' $package 'Other' } $false

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
