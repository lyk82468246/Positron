# Consume a fresh formal Debug deployment. Never build, force-close a process,
# select a device, overwrite an existing DB or remove the deployment/DB.
param(
    [Parameter(Mandatory=$true)][string] $RunRoot,
    [switch] $ConfirmedExclusiveWindow
)
$ErrorActionPreference = 'Stop'
if (!$ConfirmedExclusiveWindow) { throw 'Require an exclusive build/device window.' }
if ([Environment]::Is64BitProcess) { throw 'Use 32-bit Windows PowerShell.' }
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$runs = [IO.Path]::GetFullPath((Join-Path $repo 'tmp\device-runs')) + '\'
$run = [IO.Path]::GetFullPath($RunRoot)
if (!$run.StartsWith($runs, [StringComparison]::OrdinalIgnoreCase) -or
        $run.Substring($runs.Length) -notmatch '^\d{8}-\d{6}-app-settings-live-[a-z0-9-]+$') {
    throw 'Require a dedicated fresh app-settings-live-* formal deployment.'
}
$audit = Get-Content -LiteralPath (Join-Path $run 'module-audit-result.txt') -Raw -Encoding UTF8
$rootMatch = [regex]::Match($audit, '(?m)^remote_root=([^\r\n]+)\r?$')
if ($audit -notmatch '(?m)^status=PASS\r?$' -or
        $audit -notmatch 'holders=0 unavailable=0' -or !$rootMatch.Success) {
    throw 'Require matching successful formal module audit.'
}
$remote = $rootMatch.Groups[1].Value
if ($remote -notmatch '^\\Storage Card\\Temp\\Positron-device-gate\\app-settings-live-[a-z0-9-]+-\d{8}-\d{6}$') {
    throw 'Require a dedicated SD test deployment, not a user installation.'
}
$evidence = Join-Path $run 'app-settings-live'
if (Test-Path -LiteralPath $evidence) { throw 'Do not overwrite prior evidence.' }
[void][IO.Directory]::CreateDirectory($evidence)
$checks = [Collections.Generic.List[string]]::new()
$checks.Add('status=FAIL')
$connected = $false
function Write-Stage([string] $message) {
    Write-Host ('[app-settings-live] ' + $message)
}
function Test-LiveExitPending([string] $body, [uint32] $appPid) {
    $summary = [regex]::Match($body, '(?m)^module_audit holders=(\d+) unavailable=0\r?$')
    $holders = [regex]::Matches($body, '(?m)^module_holder pid=(\d+) process=positron\.exe module=[^\r\n]+\r?$')
    if (!$summary.Success -or $appPid -eq 0 -or $holders.Count -eq 0 -or
            ([regex]::Matches($body, '(?m)^module_audit\b')).Count -ne 1 -or
            ([regex]::Matches($body, '(?m)^module_holder\b')).Count -ne $holders.Count -or
            $holders.Count -ne [int]$summary.Groups[1].Value) { return $false }
    foreach ($holder in $holders) {
        if ([uint32]$holder.Groups[1].Value -ne $appPid) { return $false }
    }
    return $true
}
function Wait-LiveExit([string] $mode, [uint32] $appPid) {
    # A terminal fixture result precedes posted WM_CLOSE and async worker
    # drainage. Only this exact process may temporarily retain DLLs; all
    # other holders/unavailable snapshots still fail immediately.
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    $attempt = 0
    do {
        ++$attempt
        $auditLog = $remote + '\module-audit.log'
        $localLog = Join-Path $evidence ('audit-after-' + $mode + '-' + $attempt + '.log')
        [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
        try {
            [void](Invoke-RemoteModuleAudit ($remote + '\positron_process_cleanup.exe') $auditLog $localLog 10)
            return
        } catch {
            if ($_.Exception.Message -notmatch 'did not confirm an empty holder set' -or
                    !(Test-LiveExitPending (Get-Content -LiteralPath $localLog -Raw -Encoding UTF8) $appPid)) { throw }
        }
        Start-Sleep -Milliseconds 250
    } while ($deadline.Elapsed.TotalSeconds -lt 30)
    throw ('Normal process exit did not complete: ' + $mode)
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
    [PositronDeviceRapi]::Connect()
    $connected = $true
    $before = [PositronDeviceRapi]::SnapshotCrashDumps()
    $files = @('positron.exe','positron_tls.dll','positron_json.dll','positron_db.dll',
        'positron_http.dll','positron_image.dll','positron_media.dll','positron_script.dll',
        'positron_core.dll','positron_browser.dll')
    foreach ($name in $files) {
        $local = Join-Path $evidence $name
        $stage = Join-Path (Join-Path $run 'stage') $name
        $component = [IO.Path]::GetFileNameWithoutExtension($name)
        $binary = if ($name -eq 'positron.exe') {
            Join-Path $repo 'positron_app\bin\Debug\positron.exe'
        } else { Join-Path $repo ($component + '\bin\Debug\' + $name) }
        $expected = (Get-FileHash -LiteralPath $stage).Hash
        if ((Get-FileHash -LiteralPath $binary).Hash -ne $expected -or
                ![PositronDeviceRapi]::TryCopyFileFromDevice(($remote + '\' + $name), $local) -or
                (Get-FileHash -LiteralPath $local).Hash -ne $expected) {
            throw ('Mixed formal/device package: ' + $name)
        }
    }
    $checks.Add('binary_roundtrip=10/10')
    [void][PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log',
        (Join-Path $evidence 'prior.log'))
    $modes = @('create','reopen','drain','default')
    for ($index = 0; $index -lt $modes.Count; ++$index) {
        $mode = $modes[$index]
        $auditLog = $remote + '\module-audit.log'
        [PositronDeviceRapi]::DeleteFileIfExists($auditLog)
        [void](Invoke-RemoteModuleAudit ($remote + '\positron_process_cleanup.exe') $auditLog (Join-Path $evidence ('audit-before-' + $mode + '.log')) 30)
        [PositronDeviceRapi]::DeleteFileIfExists('\Temp\positron-debug.log')
        $appPid = [PositronDeviceRapi]::LaunchProcess(($remote + '\positron.exe'),
            $null, ('--selftest-settings-live-' + $mode))
        $log = Join-Path $evidence ($mode + '.log')
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $complete = $false
        do {
            if ([PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log', $log)) {
                $body = Get-Content -LiteralPath $log -Raw -Encoding UTF8
                if ($body -match 'selftest FAILED|settings-live FAIL') { throw 'Live EXE fixture failed.' }
                if ($body -match 'tabs selftest|internal-pages selftest|https://example\.com/') {
                    throw 'Settings startup was polluted by the UI fixture.'
                }
                $number = $index + 1
                $page = if ($index -lt 2) { 1 } else { 0 }
                $drained = if ($index -in @(0,2)) { 1 } else { 0 }
                $pattern = '(?m)^positron pid=' + $appPid + ' tick=\d+ settings-live PASS mode=' +
                    $number + ' history=1 page=' + $page + ' drained=' + $drained + '\r?$'
                if (([regex]::Matches($body, $pattern)).Count -eq 1 -and
                        $body -match ('debug-session pid=' + $appPid + '\b') -and
                        $body -match 'startup-window visible=1 foreground=1 page_visible=1' -and
                        $body -notmatch 'tabs selftest|internal-pages selftest|https://example\.com/') {
                    $complete = $true
                    break
                }
            }
            Start-Sleep -Milliseconds 250
        } while ($timer.Elapsed.TotalSeconds -lt 150)
        if (!$complete) { throw ('Missing exact current-process completion: ' + $mode) }
        Wait-LiveExit $mode $appPid
        if (![PositronDeviceRapi]::TryCopyFileFromDevice('\Temp\positron-debug.log', $log)) {
            throw 'Cannot retrieve the final closed-process log.'
        }
        $body = Get-Content -LiteralPath $log -Raw -Encoding UTF8
        if (([regex]::Matches($body, $pattern)).Count -ne 1 -or
                ([regex]::Matches($body, 'debug-session pid=')).Count -ne 1 -or
                $body -match 'selftest FAILED|settings-live FAIL|tabs selftest|internal-pages selftest|https://example\.com/') {
            throw 'Invalid final closed-process evidence.'
        }
        if (![PositronDeviceRapi]::TryCopyFileFromDevice(($remote + '\positron.db'),
                (Join-Path $evidence ($mode + '.db')))) { throw 'Cannot read back closed database.' }
        $checks.Add($mode + '=PASS pid=' + $appPid)
    }
    $after = [PositronDeviceRapi]::SnapshotCrashDumps()
    foreach ($dump in $after.Keys) {
        if (!$before.ContainsKey($dump) -or $before[$dump] -ne $after[$dump]) {
            throw ('New/changed crash dump: ' + $dump)
        }
    }
    $checks.Add('crash_check=PASS')
    $checks.Add('database=preserved_with_default_newtab')
    $checks.Add('fault_injection=deferred_not_passed')
    $checks[0] = 'status=PASS'
} catch {
    $checks.Add('failure=' + $_.Exception.Message)
    Write-Host $_.Exception.Message
} finally {
    if ($connected) { [PositronDeviceRapi]::Disconnect() }
    [IO.File]::WriteAllLines((Join-Path $evidence 'result.txt'), $checks)
}
if ($checks[0] -ne 'status=PASS') { exit 1 }
Write-Host ('[app-settings-live] PASS evidence=' + $evidence)
