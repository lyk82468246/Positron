param(
    [Parameter(Mandatory=$true)][string] $RunRoot,
    [ValidateSet('Debug', 'Release')][string] $Configuration = 'Debug',
    [ValidateSet('Unicode', 'Locks', 'Journal')][string] $Suite = 'Unicode',
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
        [string] $Storage, [string] $RemoteRoot, [string] $Suite = 'Unicode') {
    if ($Suite -notin @('Unicode', 'Locks', 'Journal')) { throw 'Unknown probe suite.' }
    if (([regex]::Matches($Body, '(?m)^DB_FILE_PROBE PASS\r?$')).Count -ne 1 -or
            $Body -match '(?m)^(?:FAIL|DB_FILE_PROBE FAIL|child_timeout=FAIL)') {
        throw 'Missing, duplicated or failed coordinator completion.'
    }
    $identity = [regex]::Matches($Body, '(?m)^process pid=(\d+) path=(.+)\r?$')
    $summary = [regex]::Matches($Body, '(?m)^fresh_processes=3 creator_pid=(\d+) reader_pid=(\d+) verifier_pid=(\d+) creator_exited_before_reader=1 reader_exited_before_verifier=1\r?$')
    if ($Suite -eq 'Locks') {
        $summary = [regex]::Matches($Body, '(?m)^lock_processes=6 rw_pair=concurrent ww_pair=concurrent cold_verifier_after_all_exits=1\r?$')
        if (([regex]::Matches($Body, '(?m)^lock_suite=rw-ww-v1\r?$')).Count -ne 1) {
            throw 'Missing exact lock suite identity.'
        }
    }
    if ($Suite -eq 'Journal') {
        $summary = [regex]::Matches($Body, '(?m)^journal_processes=5 writer_terminated_before_recovery=1 snapshots_before_recovery=1 cold_verifier_after_recovery_exit=1\r?$')
        foreach ($marker in @('journal_suite=hot-rollback-v1',
                'journal_snapshots=PASS before_and_hot_pair=1 recovery_not_started=1')) {
            if (([regex]::Matches($Body, '(?m)^' + [regex]::Escape($marker) + '\r?$')).Count -ne 1) {
                throw 'Missing exact journal suite/snapshot evidence.'
            }
        }
    }
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
    $pids = @()
    $roles = @('create', 'read', 'verify-b')
    if ($Suite -eq 'Unicode') {
        $pids = @([uint32]$summary[0].Groups[1].Value,
            [uint32]$summary[0].Groups[2].Value, [uint32]$summary[0].Groups[3].Value)
    }
    if ($Suite -ne 'Unicode') {
        $roles = if ($Suite -eq 'Locks') {
            @('create','rw-reader','rw-writer','ww-owner','ww-contender','lock-verify')
        } else { @('create','journal-seed','journal-writer','journal-recover','journal-verify') }
        $pids = @()
        foreach ($role in $roles) {
            $created = [regex]::Matches($Body, '(?m)^child role=' + $role + ' pid=(\d+) created=1\r?$')
            if ($created.Count -ne 1) { throw 'Missing or duplicated lock process.' }
            $pids += [uint32]$created[0].Groups[1].Value
        }
    }
    if (($pids | Select-Object -Unique).Count -ne $roles.Count -or $pids -contains $ParentPid -or
            $pids -contains 0 -or $ParentPid -eq 0) {
        throw 'Fresh process identities are not independent.'
    }
    $childLines = [regex]::Matches($Body, '(?m)^child role=[^\r\n]+\r?$')
    if ($childLines.Count -ne (2 * $roles.Count)) { throw 'Unexpected extra or missing child results.' }
    $previousEnd = -1
    $starts = @()
    $ends = @()
    for ($index = 0; $index -lt $roles.Count; ++$index) {
        $created = [regex]::Matches($Body, '(?m)^child role=' + $roles[$index] +
            ' pid=' + $pids[$index] + ' created=1\r?$')
        $pattern = '(?m)^child role=' + $roles[$index] + ' pid=' + $pids[$index] +
            ' wait=0 exit=0 completed=1\r?$'
        if ($Suite -eq 'Journal' -and $roles[$index] -eq 'journal-writer') {
            $pattern = '(?m)^child role=journal-writer pid=' + $pids[$index] +
                ' wait=0 exit=1346650698 completed=1 terminated_owned=1\r?$'
        }
        $completed = [regex]::Matches($Body, $pattern)
        if ($created.Count -ne 1 -or $completed.Count -ne 1 -or
                ($Suite -ne 'Locks' -and $created[0].Index -le $previousEnd) -or
                $completed[0].Index -le $created[0].Index) {
            throw 'A child has no exact signalled-process/zero-exit result.'
        }
        $previousEnd = $completed[0].Index
        $starts += $created[0].Index
        $ends += $completed[0].Index
    }
    if ($Suite -eq 'Locks') {
        if (!($starts[0] -lt $ends[0] -and $ends[0] -lt $starts[1] -and
                $starts[1] -lt $starts[2] -and $starts[2] -lt $ends[1] -and
                $ends[1] -lt $ends[2] -and $ends[2] -lt $starts[3] -and
                $starts[3] -lt $starts[4] -and $starts[4] -lt $ends[3] -and
                $ends[3] -lt $ends[4] -and $ends[4] -lt $starts[5])) {
            throw 'Lock competitors did not overlap or verifier ran before both pairs exited.'
        }
    }
    if ($Suite -eq 'Journal') {
        $header = [regex]::Matches($Body, '(?m)^hot_journal magic=valid bytes=(\d+) records=(\d+) original_pages=(\d+) sector=(\d+) page_size=4096 writer_exited=1\r?$')
        $snapshot = [regex]::Match($Body, '(?m)^journal_snapshots=PASS[^\r\n]*\r?$')
        if ($header.Count -ne 1 -or $header[0].Index -le $ends[2] -or
                $snapshot.Index -le $header[0].Index -or $starts[3] -le $snapshot.Index -or
                [int]$header[0].Groups[1].Value -le 512 -or
                [int]$header[0].Groups[1].Value -gt 1048576 -or
                [int]$header[0].Groups[2].Value -lt 1 -or
                [int]$header[0].Groups[2].Value -gt 128 -or
                [int]$header[0].Groups[3].Value -lt 1 -or
                [int]$header[0].Groups[3].Value -gt 128) {
            throw 'Missing valid hot journal after writer exit and before recovery.'
        }
    }
    return @{Root=$fixture; Pids=$pids}
}

function Assert-ChildLog([string] $Body, [uint32] $ChildPid, [string] $RemoteRoot,
        [string] $DatabasePath, [string] $Role) {
    $lockRole = $Role -in @('rw-reader','rw-writer','ww-owner','ww-contender','lock-verify')
    $journalRole = $Role -in @('journal-seed','journal-writer','journal-recover','journal-verify')
    $writer = $Role -eq 'journal-writer'
    if ($Role -notin @('create', 'read', 'verify-b') -and !$lockRole -and !$journalRole -or $ChildPid -eq 0) {
        throw 'Unknown child role or invalid PID.'
    }
    $identity = [regex]::Matches($Body, '(?m)^process pid=(\d+) path=(.+)\r?$')
    if ($identity.Count -ne 1 -or [uint32]$identity[0].Groups[1].Value -ne $ChildPid -or
            $identity[0].Groups[2].Value.TrimEnd("`r") -ine ($RemoteRoot + '\positron_db_file_probe.exe') -or
            $Body -match '(?m)^(?:FAIL|DB_FILE_CHILD FAIL|open_failure)' -or
            (!$writer -and ([regex]::Matches($Body, '(?m)^DB_FILE_CHILD PASS closed_before_exit=1\r?$')).Count -ne 1) -or
            ($writer -and $Body -match '(?m)^DB_FILE_CHILD PASS') -or
            (!$lockRole -and !$journalRole -and $Body -notmatch '(?m)^exact_utf8_text=PASS exact_blob=PASS schema=PASS integrity=ok transaction_idle=1 statements=0\r?$') -or
            ($lockRole -and ([regex]::Matches($Body, '(?m)^lock_integrity=ok transaction_idle=1 statements=0\r?$')).Count -ne 1) -or
            ($journalRole -and !$writer -and ([regex]::Matches($Body, '(?m)^journal_integrity=ok transaction_idle=1 statements=0\r?$')).Count -ne 1)) {
        throw 'Incomplete/failed child byte, schema, integrity or release evidence.'
    }
    if ($journalRole) {
        $markers = switch ($Role) {
            'journal-seed' { @('journal_seed=PASS rows=64 blob_bytes=2048 schema=2') }
            'journal-writer' { @('journal_writer_ready active=1 txn=2 statements=0 commit=0 rollback=0 close=0') }
            'journal-recover' { @('journal_recovered=PASS rows=64 sample_rows=1 schema=2','journal_post_recovery_commit=PASS') }
            'journal-verify' { @('journal_cold_verify=PASS rows=64 sample_rows=2 schema=2') }
        }
        foreach ($marker in $markers) {
            if (([regex]::Matches($Body, '(?m)^' + [regex]::Escape($marker) + '\r?$')).Count -ne 1) {
                throw 'Missing exact journal transaction/recovery evidence.'
            }
        }
    }
    if ($lockRole) {
        $markers = switch ($Role) {
            'rw-reader' { @('rw_old_snapshot=PASS','rw_new_snapshot=PASS') }
            'rw-writer' { @('rw_commit_retry=PASS') }
            'ww-owner' { @('ww_owner_commit=PASS') }
            'ww-contender' { @('ww_begin_retry=PASS') }
            'lock-verify' { @('locks_cold_reopen=PASS rows=4 schema=1') }
        }
        foreach ($marker in $markers) {
            if (([regex]::Matches($Body, '(?m)^' + [regex]::Escape($marker) + '\r?$')).Count -ne 1) {
                throw 'Missing or duplicated lock recovery/visibility evidence.'
            }
        }
        $busyLines = [regex]::Matches($Body, '(?m)^lock_busy[^\r\n]*\r?$')
        if ($Role -in @('rw-writer','ww-contender')) {
            $phase = if ($Role -eq 'rw-writer') { 'rw-commit' } else { 'ww-begin' }
            $state = if ($Role -eq 'rw-writer') { 'active=1 txn=2' } else { 'active=0 txn=0' }
            $busy = [regex]::Matches($Body, '(?m)^lock_busy phase=' + $phase +
                ' result=-4 category=5 native=5 extended=(\d+) ' + $state +
                ' statements=0 cleanup=0\r?$')
            if ($busyLines.Count -ne 1 -or $busy.Count -ne 1 -or
                    ([int]$busy[0].Groups[1].Value -band 255) -ne 5) {
                throw 'Missing exact native BUSY and live transaction state.'
            }
        } elseif ($busyLines.Count) { throw 'Unexpected BUSY evidence for lock role.' }
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

function Assert-JournalFiles([string] $Directory) {
    $before = Join-Path $Directory 'before.sqlite'
    $hot = Join-Path $Directory 'hot.sqlite'
    $journal = Join-Path $Directory 'hot.sqlite-journal'
    foreach ($file in @($before, $hot, $journal)) {
        $limit = if ($file -eq $journal) { 1048576 } else { 524288 }
        $length = (Get-Item -LiteralPath $file).Length
        if ($length -le 512 -or $length -gt $limit) { throw 'Raw snapshot size is outside fixed bounds.' }
    }
    if ((Get-FileHash -LiteralPath $before).Hash -eq (Get-FileHash -LiteralPath $hot).Hash) {
        throw 'No actual uncommitted database spill; journal existence alone is insufficient.'
    }
    $bytes = [IO.File]::ReadAllBytes($journal)
    $magic = [byte[]](0xd9,0xd5,0x05,0xf9,0x20,0xa1,0x63,0xd7)
    for ($index = 0; $index -lt 8; ++$index) {
        if ($bytes[$index] -ne $magic[$index]) { throw 'Invalid raw journal magic.' }
    }
    $values = @()
    foreach ($offset in @(8,16,20,24)) {
        $values += ([long]$bytes[$offset] * 16777216 + [long]$bytes[$offset+1] * 65536 +
            [long]$bytes[$offset+2] * 256 + $bytes[$offset+3])
    }
    $records, $pages, $sector, $pageSize = $values
    if ($records -lt 1 -or $records -gt 128 -or $pages -lt 1 -or $pages -gt 128 -or
            $sector -lt 512 -or $sector -gt 65536 -or ($sector -band ($sector-1)) -ne 0 -or
            $pageSize -ne 4096 -or $bytes.Length -lt ($sector + $records * 4104) -or
            (Get-Item -LiteralPath $before).Length -ne ($pages * 4096)) {
        throw 'Raw journal header/record bounds disagree with baseline database.'
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
    foreach ($name in @('create.log', 'read.log', 'verify-b.log', 'owner.marker',
            'rw-reader.log','rw-writer.log','ww-owner.log','ww-contender.log','lock-verify.log',
            'journal-seed.log','journal-writer.log','journal-recover.log','journal-verify.log',
            'before.sqlite','hot.sqlite','hot.sqlite-journal')) {
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
        $command = '--run-' + $Suite.ToLowerInvariant()
        if ($storage -eq 'internal') { $command += '-internal' }
        $probePid = [PositronDeviceRapi]::LaunchProcess(
            ($remote + '\positron_db_file_probe.exe'), $remote, $command)
        $logName = 'db-file-probe-' + $storage + '.log'
        $localCoordinatorLog = Join-Path $evidence $logName
        $body = Wait-ProbeLog ($remote + '\' + $logName) `
            $localCoordinatorLog 210
        $summary = Assert-ProbeLog $body $probePid $storage $remote $Suite
        $fixtures.Add($summary.Root)
        $destination = Join-Path $evidence $storage
        [void][IO.Directory]::CreateDirectory($destination)
        $chineseDirectory = ([string][char]0x4e2d) + [char]0x6587 + [char]0x76ee + [char]0x5f55
        $chineseDatabase = ([string][char]0x6570) + [char]0x636e + [char]0x5e93 + '.sqlite'
        $dbPath = $summary.Root + '\' + $chineseDirectory + '\' + $chineseDatabase
        $roles = @('create', 'read', 'verify-b')
        if ($Suite -eq 'Locks') {
            $roles = @('create','rw-reader','rw-writer','ww-owner','ww-contender','lock-verify')
        }
        if ($Suite -eq 'Journal') {
            $roles = @('create','journal-seed','journal-writer','journal-recover','journal-verify')
        }
        for ($index = 0; $index -lt $roles.Count; ++$index) {
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
        if ($Suite -eq 'Journal') {
            foreach ($name in @('before.sqlite','hot.sqlite','hot.sqlite-journal')) {
                $hash = Copy-StableRemoteFile ($summary.Root + '\' + $name) (Join-Path $destination $name)
                $checks.Add($storage + '_snapshot_sha256=' + $name + ':' + $hash)
            }
            Assert-JournalFiles $destination
            $checks.Add($storage + '_hot_journal_spill_recovery=PASS')
        }
        $checks.Add($storage + '_database_path=' + $dbPath)
        $checks.Add($storage + '_database_sha256=' + $dbHash)
        $checks.Add($storage + '_fresh_processes=' + $roles.Count)
        $checks.Add($storage + '_suite=' + $Suite)
        $checks.Add($storage + '_text_blob_schema_integrity=PASS')
        if ($Suite -eq 'Locks') { $checks.Add($storage + '_rw_ww_busy_visibility_recovery=PASS') }
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
