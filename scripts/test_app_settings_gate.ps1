$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'app_settings_gate.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$fn = $ast.Find({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Assert-SettingsFixtureLog'
}, $false)
if (!$fn) { throw 'Missing actual gate validator.' }
. ([scriptblock]::Create($fn.Extent.Text))
$prefix = "positron debug-session pid=12 tick=1`r`n"
$end = "positron pid=12 tick=2 settings-fixture PASS exit=0`r`n"
$storage = $prefix + "positron pid=12 tick=2 positron settings-storage selftest OK phase=6 cleanup=0 same_process_reopen=1 production_enabled=0`r`n" + $end
$services = $prefix + "positron pid=12 tick=2 positron settings-services selftest OK phase=4 line=0 cleanup=0 production_enabled=0`r`n" + $end
Assert-SettingsFixtureLog $storage 12 storage
Assert-SettingsFixtureLog $services 12 services
$count = 2
foreach ($body in @($storage.Replace('pid=12','pid=13'),
        $storage.Replace('cleanup=0','cleanup=1'),
        $storage.Replace('production_enabled=0','production_enabled=1'),
        $storage.Replace('same_process_reopen=1','same_process_reopen=0'),
        $storage.Replace('phase=6','phase=2'),
        $storage.Replace($end,''), ($storage + $end),
        ($storage + "settings-fixture FAIL`r`n"), ($storage + $prefix), $services,
        $storage.Replace('exit=0','exit=1'))) {
    $refused = $false
    try { Assert-SettingsFixtureLog $body 12 storage } catch { $refused = $true }
    if (!$refused) { throw ('Actual gate validator accepted invalid evidence at check ' + $count) }
    ++$count
}
Write-Host ('App settings gate validator PASS checks=' + $count)
