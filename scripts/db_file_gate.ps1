param(
    [Parameter(Mandatory=$true)][string] $RunRoot,
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Debug',
    [switch] $ConfirmedExclusiveWindow,
    [switch] $PreserveDeployment
)
$ErrorActionPreference = 'Stop'

# This script consumes a fresh, preserved formal ModuleAuditOnly stage. It
# never builds, selects a device, resets it or terminates a caller-supplied PID.
if (!$ConfirmedExclusiveWindow) {
    throw 'An explicitly coordinated exclusive build/device window is required.'
}
if ([IntPtr]::Size -ne 4) {
    throw 'Use 32-bit Windows PowerShell, as for the formal device gate.'
}
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runs = [IO.Path]::GetFullPath((Join-Path $repo 'tmp\device-runs')) + '\'
$run = [IO.Path]::GetFullPath($RunRoot)
if (!$run.StartsWith($runs, [StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-db-file-[a-z0-9-]+$') {
    throw 'RunRoot must be one dedicated db-file-* formal gate directory.'
}
$sln = Get-Content -LiteralPath (Join-Path $repo 'Positron.sln') -Raw -Encoding UTF8
if ($sln -notmatch 'D8C04756-1F83-4A30-9C18-F6E7A49F3120') {
    throw 'DB file probe is not registered in the formal solution yet.'
}
$audit = @{}
foreach ($line in (Get-Content -LiteralPath (Join-Path $run 'module-audit-result.txt') -Encoding UTF8)) {
    if ($line -match '^([^=]+)=(.*)$') { $audit[$matches[1]] = $matches[2] }
}
if ($audit['status'] -ne 'PASS' -or
        $audit['current_cleanup'] -ne 'preserved_for_diagnosis') {
    throw 'Require a fresh, preserved ModuleAuditOnly deployment.'
}
$remote = [string]$audit['remote_root']
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\db-file-[a-z0-9-]+-\d{8}-\d{6}$') {
    throw 'Binary deployment must use an explicit dedicated SD gate root.'
}
$stage = Join-Path $run 'stage'
$evidence = Join-Path $run 'db-file-evidence'
if (Test-Path -LiteralPath $evidence) { throw 'Never overwrite prior DB file evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$resultFile = Join-Path $evidence 'result.txt'
$checks = [Collections.Generic.List[string]]::new()
$checks.Add('status=FAIL')
$connected = $false
$accepted = $false
$crashBefore = $null
$probePid = 0
$storage = ''
$localCoordinatorLog = ''

function Write-Stage([string] $message) { Write-Host ('[db-file-gate] ' + $message) }

function Copy-StableRemoteFile([string] $RemotePath, [string] $LocalPath) {
    $second = $LocalPath + '.second'
    if (![PositronDeviceRapi]::TryCopyFileFromDevice($RemotePath, $LocalPath) -or
            ![PositronDeviceRapi]::TryCopyFileFromDevice($RemotePath, $second)) {
        throw "Cannot retrieve complete fixture evidence: $RemotePath"
    }
    $hash = (Get-FileHash -LiteralPath $LocalPath -Algorithm SHA256).Hash
    if ($hash -ne (Get-FileHash -LiteralPath $second -Algorithm SHA256).Hash) {
        throw "Fixture evidence is still changing: $RemotePath"
    }
    return $hash
}

function Wait-ProbeLog([string] $RemotePath, [string] $LocalPath, [int] $TimeoutSeconds) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice($RemotePath, $LocalPath)) {
            $body = Get-Content -LiteralPath $LocalPath -Raw -Encoding UTF8
            if ($body -match '(?m)^DB_FILE_PROBE FAIL\r?$') { throw 'Fixture coordinator failed.' }
            if ($body -match '(?m)^DB_FILE_PROBE PASS\r?$') {
                [void](Copy-StableRemoteFile $RemotePath $LocalPath)
                return (Get-Content -LiteralPath $LocalPath -Raw -Encoding UTF8)
            }
        }
        Start-Sleep -Milliseconds 250
    }
    # The ARM coordinator owns timeout/termination for its own children.
    # Do not kill the coordinator or any other application from the host.
    throw 'No complete DB file coordinator result before the bounded deadline.'
}

function Assert-ProbeLog([string] $Body, [uint32] $ParentPid,
        [string] $Storage, [string] $RemoteRoot) {
    if (([regex]::Matches($Body, '(?m)^DB_FILE_PROBE PASS\r?$')).Count -ne 1 -or
            $Body -match '(?m)^(?:FAIL|DB_FILE_PROBE FAIL|child_timeout=FAIL)') {
        throw 'Missing, duplicated or failed coordinator completion.'
    }
    $identity = [regex]::Matches($Body, '(?m)^process pid=(\d+) path=(.+)\r?$')
    $summary = [regex]::Matches($Body, '(?m)^fresh_processes=3 creator_pid=(\d+) reader_pid=(\d+) verifier_pid=(\d+) creator_exited_before_reader=1 reader_exited_before_verifier=1\r?$')
    $rootMatch = [regex]::Matches($Body, '(?m)^fixture_root=(.+)\r?$')
    $storageMatches = [regex]::Matches($Body, '(?m)^fixture_storage=([^\r\n]+)\r?$')
    if ($identity.Count -ne 1 -or $summary.Count -ne 1 -or $rootMatch.Count -ne 1 -or
            [uint32]$identity[0].Groups[1].Value -ne $ParentPid -or
            $identity[0].Groups[2].Value.TrimEnd("`r") -ine ($RemoteRoot + '\positron_db_file_probe.exe') -or
            $storageMatches.Count -ne 1 -or $storageMatches[0].Groups[1].Value -cne $Storage) {
        throw 'Unexpected coordinator identity, storage or process ordering.'
    }
    $fixture = $rootMatch[0].Groups[1].Value.TrimEnd("`r")
    $base = if ($Storage -eq 'sd') { $RemoteRoot } else {
        '\Temp\Positron-device-gate\db-file-fixtures'
    }
    if ($fixture -notmatch ('^' + [regex]::Escape($base) + '\\fixture-' + $ParentPid + '-\d{1,10}$')) {
        throw 'Fixture root is not the directory owned by this exact coordinator.'
    }
    $pids = @([uint32]$summary[0].Groups[1].Value,
        [uint32]$summary[0].Groups[2].Value, [uint32]$summary[0].Groups[3].Value)
    if (($pids | Select-Object -Unique).Count -ne 3 -or $pids -contains $ParentPid -or
            $pids -contains 0 -or $ParentPid -eq 0) {
        throw 'Fresh process identities are not independent.'
    }
    $roles = @('create', 'read', 'verify-b')
    $childLines = [regex]::Matches($Body, '(?m)^child role=[^\r\n]+\r?$')
    if ($childLines.Count -ne 6) { throw 'Unexpected extra or missing child results.' }
    $previousEnd = -1
    for ($index = 0; $index -lt 3; ++$index) {
        $created = [regex]::Matches($Body, '(?m)^child role=' + $roles[$index] +
            ' pid=' + $pids[$index] + ' created=1\r?$')
        $pattern = '(?m)^child role=' + $roles[$index] + ' pid=' + $pids[$index] +
            ' wait=0 exit=0 completed=1\r?$'
        $completed = [regex]::Matches($Body, $pattern)
        if ($created.Count -ne 1 -or $completed.Count -ne 1 -or
                $created[0].Index -le $previousEnd -or $completed[0].Index -le $created[0].Index) {
            throw 'A child has no exact signalled-process/zero-exit result.'
        }
        $previousEnd = $completed[0].Index
    }
    return @{Root=$fixture; Pids=$pids}
}

function Assert-ChildLog([string] $Body, [uint32] $ChildPid, [string] $RemoteRoot,
        [string] $DatabasePath, [string] $Role) {
    if ($Role -notin @('create', 'read', 'verify-b') -or $ChildPid -eq 0) {
        throw 'Unknown child role or invalid PID.'
    }
    $identity = [regex]::Matches($Body, '(?m)^process pid=(\d+) path=(.+)\r?$')
    if ($identity.Count -ne 1 -or [uint32]$identity[0].Groups[1].Value -ne $ChildPid -or
            $identity[0].Groups[2].Value.TrimEnd("`r") -ine ($RemoteRoot + '\positron_db_file_probe.exe') -or
            $Body -match '(?m)^(?:FAIL|DB_FILE_CHILD FAIL|open_failure)' -or
            ([regex]::Matches($Body, '(?m)^DB_FILE_CHILD PASS closed_before_exit=1\r?$')).Count -ne 1 -or
            $Body -notmatch '(?m)^exact_utf8_text=PASS exact_blob=PASS schema=PASS integrity=ok transaction_idle=1 statements=0\r?$') {
        throw 'Incomplete/failed child byte, schema, integrity or release evidence.'
    }
    if ($Role -eq 'create' -and
            ([regex]::Matches($Body, '(?m)^migration_script_failure=atomic\r?$')).Count -ne 1) {
        throw 'Missing exact atomic migration failure evidence.'
    }
    if ($Role -eq 'create' -and
            ([regex]::Matches($Body, '(?m)^file_identity_guard=CREATE_NEW\r?$')).Count -ne 1) {
        throw 'Missing exclusive database filename reservation.'
    }
    foreach ($pair in @(@('db_module_path', ($RemoteRoot + '\positron_db.dll')),
            @('fixture_db_utf8', $DatabasePath))) {
        $found = [regex]::Matches($Body, '(?m)^' + $pair[0] + '=(.+)\r?$')
        if ($found.Count -ne 1 -or $found[0].Groups[1].Value.TrimEnd("`r") -ine $pair[1]) {
            throw 'Child used a different DB module or file path.'
        }
    }
}

function Save-FailedProbeEvidence([string] $ParentLog, [uint32] $OwnerPid,
        [string] $Storage, [string] $RemoteRoot, [string] $Destination) {
    if ($OwnerPid -eq 0 -or $Storage -notin @('sd', 'internal') -or
            !(Test-Path -LiteralPath $ParentLog)) { return }
    $body = Get-Content -LiteralPath $ParentLog -Raw -Encoding UTF8
    $match = [regex]::Matches($body, '(?m)^fixture_root=([^\r\n]+)\r?$')
    if ($match.Count -ne 1) { return }
    $base = if ($Storage -eq 'sd') { $RemoteRoot } else {
        '\Temp\Positron-device-gate\db-file-fixtures'
    }
    $fixture = $match[0].Groups[1].Value
    if ($fixture -notmatch ('^' + [regex]::Escape($base) + '\\fixture-' + $OwnerPid + '-\d{1,10}$')) {
        throw 'Refuse diagnostic reads outside this coordinator-owned fixture.'
    }
    [void][IO.Directory]::CreateDirectory($Destination)
    foreach ($name in @('create.log', 'read.log', 'verify-b.log', 'owner.marker')) {
        $copied = [PositronDeviceRapi]::TryCopyFileFromDevice(
            ($fixture + '\' + $name), (Join-Path $Destination $name))
        Write-Stage ('failure evidence ' + $name + ' retrieved=' + $copied)
    }
}

try {
    # Reuse the formal RAPI connection and audit implementation, not a new
    # device selector or a second interpretation of module-holder semantics.
    $gatePath = Join-Path $PSScriptRoot 'device_gate.ps1'
    $gateText = Get-Content -LiteralPath $gatePath -Raw -Encoding UTF8
    $source = [regex]::Match($gateText, "(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
    if (!$source.Success) { throw 'Missing formal RAPI implementation.' }
    Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
    $tokens = $null
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($gatePath,
            [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw $errors[0] }
    $function = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Invoke-RemoteModuleAudit'
    }, $false)
    if ($null -eq $function) { throw 'Missing formal module audit runner.' }
    . ([scriptblock]::Create($function.Extent.Text))

    $files = @('positron_db_file_probe.exe', 'positron_process_cleanup.exe',
        'positron_tls.dll', 'positron_json.dll', 'positron_db.dll', 'positron_http.dll',
        'positron_image.dll', 'positron_media.dll', 'positron_script.dll',
        'positron_core.dll', 'positron_browser.dll')
    foreach ($name in $files) {
        Write-Stage ('checking formal stage ' + $name)
        if (!(Test-Path -LiteralPath (Join-Path $stage $name))) {
            throw "Formal stage is missing $name; do not assemble a partial package."
        }
    }
    foreach ($name in ($files | Where-Object { $_ -like '*.dll' })) {
        $component = [IO.Path]::GetFileNameWithoutExtension($name)
        $current = Join-Path $repo ($component + '\bin\' + $Configuration + '\' + $name)
        if ((Get-FileHash -LiteralPath $current).Hash -ne
                (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash) {
            throw 'Stage no longer matches the current formal DLL configuration.'
        }
    }
    $probeBuild = Join-Path $repo ('device_tools\db_file_probe\bin\' +
        $Configuration + '\positron_db_file_probe.exe')
    if ((Get-FileHash -LiteralPath $probeBuild).Hash -ne
            (Get-FileHash -LiteralPath (Join-Path $stage 'positron_db_file_probe.exe')).Hash) {
        throw 'Probe stage does not match the formal helper build.'
    }

    [PositronDeviceRapi]::Connect()
    $connected = $true
    $sd = [PositronDeviceRapi]::QueryVolumeStorage($remote)
    $internal = [PositronDeviceRapi]::QueryObjectStoreStorage()
    # One 2 MiB database + a bounded rollback journal + a 1 MiB reserve on
    # each data filesystem; never consume the reserve to induce FULL.
    if (!$sd.Available -or !$internal.Available -or
            $sd.FreeBytes -lt 5242880 -or $internal.FreeBytes -lt 5242880) {
        throw 'Both data filesystems need at least 5 MiB free before this gate.'
    }
    $checks.Add('sd_api=' + $sd.Api)
    $checks.Add('sd_scope=' + $sd.Scope)
    $checks.Add('sd_total_bytes=' + $sd.TotalBytes)
    $checks.Add('sd_free_bytes=' + $sd.FreeBytes)
    $checks.Add('internal_api=' + $internal.Api)
    $checks.Add('internal_scope=' + $internal.Scope)
    $checks.Add('internal_free_bytes=' + $internal.FreeBytes)
    $checks.Add('real_volume_exhaustion=NOT_TESTED')
    $checks.Add('physical_power_loss=NOT_TESTED')
    [PositronDeviceRapi]::EnsureDirectory('\Temp\Positron-device-gate')
    [PositronDeviceRapi]::EnsureDirectory('\Temp\Positron-device-gate\db-file-fixtures')
    $hashes = [Collections.Generic.List[string]]::new()
    foreach ($name in $files) {
        $local = Join-Path $evidence $name
        $hash = Copy-StableRemoteFile ($remote + '\' + $name) $local
        if ($hash -ne (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash) {
            throw "Device round-trip hash differs: $name"
        }
        $hashes.Add($hash + '  ' + $name)
    }
    [IO.File]::WriteAllLines((Join-Path $evidence 'device-roundtrip-sha256.txt'), $hashes)
    $helper = $remote + '\positron_process_cleanup.exe'
    $auditLog = $remote + '\module-audit.log'
    # Keep the formal stage's prior audit evidence locally, but never accept
    # that old remote summary as proof for this new launch.
    [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
    [void](Invoke-RemoteModuleAudit $helper $auditLog `
            (Join-Path $evidence 'module-audit-before.log') 30)
    $crashBefore = [PositronDeviceRapi]::SnapshotCrashDumps()
    $fixtures = [Collections.Generic.List[string]]::new()
    foreach ($storage in @('sd', 'internal')) {
        $command = if ($storage -eq 'sd') { '--run-unicode' } else { '--run-unicode-internal' }
        $probePid = [PositronDeviceRapi]::LaunchProcess(
            ($remote + '\positron_db_file_probe.exe'), $remote, $command)
        $logName = 'db-file-probe-' + $storage + '.log'
        $localCoordinatorLog = Join-Path $evidence $logName
        $body = Wait-ProbeLog ($remote + '\' + $logName) `
            $localCoordinatorLog 210
        $summary = Assert-ProbeLog $body $probePid $storage $remote
        $fixtures.Add($summary.Root)
        $destination = Join-Path $evidence $storage
        [void][IO.Directory]::CreateDirectory($destination)
        $chineseDirectory = ([string][char]0x4e2d) + [char]0x6587 + [char]0x76ee + [char]0x5f55
        $chineseDatabase = ([string][char]0x6570) + [char]0x636e + [char]0x5e93 + '.sqlite'
        $dbPath = $summary.Root + '\' + $chineseDirectory + '\' + $chineseDatabase
        $roles = @('create', 'read', 'verify-b')
        for ($index = 0; $index -lt 3; ++$index) {
            $role = $roles[$index]
            $local = Join-Path $destination ($role + '.log')
            [void](Copy-StableRemoteFile ($summary.Root + '\' + $role + '.log') $local)
            Assert-ChildLog (Get-Content -LiteralPath $local -Raw -Encoding UTF8) `
                $summary.Pids[$index] $remote $dbPath $role
        }
        [void](Copy-StableRemoteFile ($summary.Root + '\owner.marker') `
            (Join-Path $destination 'owner.marker'))
        if ((Get-Content -LiteralPath (Join-Path $destination 'owner.marker') -Raw -Encoding ASCII) -cne
                "DB_FILE_FIXTURE_V1`r`n") {
            throw 'Fixture owner marker is not the exact expected format.'
        }
        $dbHash = Copy-StableRemoteFile $dbPath (Join-Path $destination $chineseDatabase)
        $checks.Add($storage + '_database_path=' + $dbPath)
        $checks.Add($storage + '_database_sha256=' + $dbHash)
        $checks.Add($storage + '_fresh_processes=3')
        $checks.Add($storage + '_exact_text_blob_schema_integrity=PASS')
    }
    [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
    [void](Invoke-RemoteModuleAudit $helper $auditLog `
        (Join-Path $evidence 'module-audit-after.log') 30)
    $crashAfter = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($dump in $crashAfter.Keys) {
        if (!$crashBefore.ContainsKey($dump) -or $crashBefore[$dump] -ne $crashAfter[$dump]) {
            throw 'A new/changed crash dump invalidates fixture acceptance.'
        }
    }
    $checks.Add('crash_check=PASS')
    $checks.Add('module_audit_before_after=PASS')
    $checks.Add('complete_log_retrieved=True')
    if ($PreserveDeployment) {
        $checks[0] = 'status=DIAGNOSTIC_ONLY'
        $checks.Add('cleanup=preserved_by_request')
    } else {
        # Every target is the exact marker/PID-validated child fixture or the
        # single db-file-* stage directory; no parent or wildcard deletion.
        foreach ($fixture in $fixtures) {
            $removed = [PositronDeviceRapi]::DeleteDirectoryTreeBestEffort($fixture)
            $checks.Add('fixture_cleanup=' + $fixture + ':' + $removed)
        }
        $removed = [PositronDeviceRapi]::DeleteDirectoryTreeBestEffort($remote)
        $checks.Add('stage_cleanup=' + $removed)
        $checks[0] = 'status=PASS'
    }
    $accepted = $true
} catch {
    $accepted = $false
    $checks[0] = 'status=FAIL'
    $checks.Add('failure=' + $_.Exception.Message)
    $checks.Add('cleanup=preserved_after_failure')
    Write-Stage $_.Exception.Message
    if ($connected) {
        try {
            Save-FailedProbeEvidence $localCoordinatorLog $probePid $storage $remote `
                (Join-Path $evidence 'failure-child')
            $failureCrashes = [PositronDeviceRapi]::SnapshotCrashDumps()
            $checks.Add('failure_crash_inventory_count=' + $failureCrashes.Count)
            foreach ($dump in $failureCrashes.Keys) {
                $checks.Add('failure_crash_file=' + $dump)
            }
        } catch { $checks.Add('failure_evidence_error=' + $_.Exception.Message) }
    }
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
    [IO.File]::WriteAllLines($resultFile, $checks)
}
if (!$accepted) { exit 1 }
Write-Stage ('evidence=' + $evidence)
exit 0
