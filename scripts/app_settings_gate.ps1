# Consume a fresh formal Debug ModuleAuditOnly package. Never builds, selects
# a device, kills a process, edits user settings or removes a deployment tree.
param(
    [Parameter(Mandatory=$true)][string] $RunRoot,
    [switch] $ConfirmedExclusiveWindow
)
$ErrorActionPreference = 'Stop'
if (!$ConfirmedExclusiveWindow) { throw 'Require a coordinated exclusive device window.' }
if ([Environment]::Is64BitProcess) { throw 'Run with 32-bit Windows PowerShell.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runs = [IO.Path]::GetFullPath((Join-Path $repo 'tmp\device-runs')) + '\'
$run = [IO.Path]::GetFullPath($RunRoot)
if (!$run.StartsWith($runs, [StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-settings-[a-z0-9-]+$') {
    throw 'Expected one dedicated app-settings-* formal gate directory.'
}
$audit = Get-Content -LiteralPath (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$rootMatch = [regex]::Match($audit, '(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or
        $audit -notmatch 'holders=0 unavailable=0' -or !$rootMatch.Success) {
    throw 'Require successful formal module-audit deployment.'
}
$remote = $rootMatch.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-settings-[a-z0-9-]+-\d{8}-\d{6}$') {
    throw 'Expected an explicit dedicated SD binary deployment.'
}
$stage = Join-Path $run 'stage'
$evidence = Join-Path $run 'app-settings'
if (Test-Path -LiteralPath $evidence) { throw 'Never overwrite prior acceptance evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$checks = [Collections.Generic.List[string]]::new()
$checks.Add('status=FAIL')
$connected = $false
$before = $null

function Write-Stage([string] $message) { Write-Host ('[app-settings-gate] ' + $message) }

function Assert-SettingsFixtureLog([string] $Body, [uint32] $AppPid, [string] $Suite) {
    if ($AppPid -eq 0 -or $Suite -notin @('storage','services',
            'visits-create','visits-reopen','visits-clear')) {
        throw 'Invalid fixture identity.'
    }
    if ($Body -match 'selftest FAILED|settings-fixture FAIL' -or
            ([regex]::Matches($Body, '(?m)^positron debug-session pid=' + $AppPid +
            ' tick=\d+\r?$')).Count -ne 1 -or
            ([regex]::Matches($Body, '(?m)^positron pid=' + $AppPid +
            ' tick=\d+ settings-fixture PASS exit=0\r?$')).Count -ne 1) {
        throw 'Missing, duplicated or failed current-process terminal result.'
    }
    $detail = if ($Suite -eq 'storage') {
        'phase=6 cleanup=0 same_process_reopen=1 production_enabled=0'
    } else { 'phase=4 line=0 cleanup=0 production_enabled=0' }
    if ($Suite.StartsWith('visits-')) {
        $mode = @{'visits-create'=1;'visits-reopen'=2;'visits-clear'=3}[$Suite]
        $pattern = '(?m)^positron pid=' + $AppPid +
            ' tick=\d+ positron visits-live selftest OK mode=' + $mode +
            ' cleanup=0 production_enabled=0\r?$'
    } else {
        $pattern = '(?m)^positron pid=' + $AppPid + ' tick=\d+ positron settings-' +
            $Suite + ' selftest OK ' + $detail + '\r?$'
    }
    if (([regex]::Matches($Body, $pattern)).Count -ne 1) {
        throw 'Missing exact suite, cleanup or production-disabled evidence.'
    }
    $preferences = if ($Suite -eq 'storage') {
        'preferences-storage selftest OK migration=v1 snapshot=atomic reject=unchanged reopen=exact legacy_preserves_policy=1 failed_write=rollback future=refused'
    } elseif ($Suite.StartsWith('visits-')) {
        'preferences-process selftest OK migration=v2 snapshot=exact legacy_visit=preserved clear_preserves_preferences=1'
    } else { $null }
    if ($null -ne $preferences -and ([regex]::Matches($Body,
            '(?m)^positron pid=' + $AppPid + ' tick=\d+ positron ' +
            [regex]::Escape($preferences) + '\r?$')).Count -ne 1) {
        throw 'Missing structured-preference migration or recovery evidence.'
    }
    if (([regex]::Matches($Body, '(?:settings-(?:storage|services)|visits-live) selftest')).Count -ne 1 -or
            ([regex]::Matches($Body, 'settings-fixture PASS')).Count -ne 1 -or
            ([regex]::Matches($Body, 'debug-session pid=')).Count -ne 1) {
        throw 'Unexpected extra fixture/session completion.'
    }
}

function Receive-SettingsFixture([string] $Suite, [uint32] $AppPid) {
    $local = Join-Path $evidence ($Suite + '.log')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 120) {
        if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log', $local)) {
            $body = Get-Content -LiteralPath $local -Raw -Encoding UTF8
            if ($body -match 'selftest FAILED|settings-fixture FAIL') {
                throw ('EXE ' + $Suite + ' fixture failed; evidence preserved.')
            }
            if ($body -match 'settings-fixture PASS') {
                Assert-SettingsFixtureLog $body $AppPid $Suite
                Start-Sleep -Milliseconds 250
                $second = $local + '.second'
                if (![PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log', $second) -or
                        (Get-FileHash -LiteralPath $local).Hash -ne
                        (Get-FileHash -LiteralPath $second).Hash) {
                    throw 'Fixture log is incomplete or still changing.'
                }
                return
            }
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Fixture timed out; do not kill a process or delete evidence.'
}

try {
    $gatePath = Join-Path $PSScriptRoot 'device_gate.ps1'
    $gate = Get-Content -LiteralPath $gatePath -Raw -Encoding UTF8
    $source = [regex]::Match($gate, "(?s)\`$rapiSource = @'\r?\n(.*?)\r?\n'@")
    if (!$source.Success) { throw 'Missing formal RAPI helper.' }
    Add-Type -TypeDefinition $source.Groups[1].Value -Language CSharp
    $tokens = $null
    $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile($gatePath, [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw 'Formal gate parse failure.' }
    $fn = $ast.Find({param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Invoke-RemoteModuleAudit'
    }, $false)
    if ($null -eq $fn) { throw 'Missing formal module-audit function.' }
    . ([scriptblock]::Create($fn.Extent.Text))
    # Reuse the already-tested exact-PID normal-exit guard, not a sleep or
    # relaxed module audit. Terminal logs precede WinMain/DLL unload.
    $exitAst = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot 'app_settings_live_gate.ps1'),
        [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw 'Live gate parse failure.' }
    foreach ($exitName in @('Test-LiveExitPending','Wait-LiveExit')) {
        $exitFn = $exitAst.Find({param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
            $node.Name -eq $exitName
        }, $false)
        if ($null -eq $exitFn) { throw 'Missing normal-exit guard.' }
        . ([scriptblock]::Create($exitFn.Extent.Text))
    }
    [PositronDeviceRapi]::Connect()
    $connected = $true
    $internal = [PositronDeviceRapi]::QueryObjectStoreStorage()
    if (!$internal.Available -or $internal.FreeBytes -lt 1048576) {
        throw 'Need at least 1 MiB internal reserve for the small isolated fixture.'
    }
    $checks.Add('internal_free_bytes=' + $internal.FreeBytes)
    $helper = $remote + '\positron_process_cleanup.exe'
    $auditLog = $remote + '\module-audit.log'
    [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
    [void](Invoke-RemoteModuleAudit $helper $auditLog (Join-Path $evidence 'audit-before.log') 30)
    $before = [PositronDeviceRapi]::SnapshotCrashDumps()
    $files = @('positron.exe','positron_tls.dll','positron_json.dll','positron_db.dll',
        'positron_http.dll','positron_image.dll','positron_media.dll','positron_script.dll',
        'positron_core.dll','positron_browser.dll')
    foreach ($name in $files) {
        $component = [IO.Path]::GetFileNameWithoutExtension($name)
        $binary = if ($name -eq 'positron.exe') {
            Join-Path $repo 'positron_app\bin\Debug\positron.exe'
        } else { Join-Path $repo ($component + '\bin\Debug\' + $name) }
        $expected = (Get-FileHash -LiteralPath (Join-Path $stage $name)).Hash
        if ((Get-FileHash -LiteralPath $binary).Hash -ne $expected) {
            throw ('Stage no longer matches formal Debug output: ' + $name)
        }
        $local = Join-Path $evidence $name
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(($remote + '\' + $name), $local) -or
                (Get-FileHash -LiteralPath $local).Hash -ne $expected) {
            throw ('Mixed or unreadable device binary: ' + $name)
        }
    }
    $checks.Add('binary_roundtrip=10/10')
    # Save the fixed global diagnostic log before any fixture starts/truncates it.
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',
        (Join-Path $evidence 'prior.log'))
    foreach ($suite in @('storage','services','visits-create','visits-reopen','visits-clear')) {
        [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
        $appPid = [PositronDeviceRapi]::LaunchProcess(($remote + '\positron.exe'),
            $null, ('--selftest-settings-' + $suite))
        Write-Stage ('running ' + $suite + ' pid=' + $appPid)
        Receive-SettingsFixture $suite $appPid
        # Require a fresh no-holder audit between the isolated processes;
        # never accept the first deployment snapshot as evidence of exit.
        Wait-LiveExit $suite $appPid
        $checks.Add($suite + '=PASS pid=' + $appPid)
    }
    $after = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($dump in $after.Keys) {
        if (!$before.ContainsKey($dump) -or $before[$dump] -ne $after[$dump]) {
            throw ('New/changed crash dump: ' + $dump)
        }
    }
    $checks.Add('crash_check=PASS')
    $checks.Add('production_enabled=0')
    $checks.Add('deployment=preserved')
    $checks[0] = 'status=PASS'
} catch {
    $checks.Add('failure=' + $_.Exception.Message)
    $checks.Add('deployment=preserved_after_failure')
    Write-Stage $_.Exception.Message
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'), $checks)
}
if ($checks[0] -ne 'status=PASS') { exit 1 }
Write-Stage ('PASS evidence=' + $evidence)
