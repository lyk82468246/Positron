# Offline contract checks for normal-exit waiting. No RAPI or device access.
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$tokens=$null; $errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'app_runtime_platform_gate.ps1'),[ref]$tokens,[ref]$errors)
if ($errors.Count) { throw 'Gate syntax failed.' }
$fn=$ast.Find({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Wait-NormalExit'},$false)
if ($null -eq $fn) { throw 'Missing production normal-exit function.' }
. ([scriptblock]::Create($fn.Extent.Text))
$evidence=Join-Path $repo ('tmp\runtime-gate-unit-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($evidence)
function Start-Sleep { param([int]$Milliseconds) }
function Get-Date {
    ++$script:clockCalls
    $base=[datetime]'2020-01-01T00:00:00'
    if ($script:timeout -and $script:clockCalls -gt 1) { return $base.AddSeconds(60) }
    return $base
}
function Assert-NoHolders([string]$tag) {
    $index=$script:observed++
    if ($index -ge $script:snapshots.Count) { throw 'Unexpected extra audit.' }
    $body=$script:snapshots[$index]
    if ($body -eq 'missing') { throw 'No audit produced.' }
    [IO.File]::WriteAllText((Join-Path $evidence ($tag+'-modules.log')),$body)
    if ($body -match 'module_audit holders=0 unavailable=0') { return }
    throw 'Audit did not confirm zero holders.'
}
function Check([string]$name,[string[]]$inputSnapshots,[bool]$success,
        [int]$expectedAudits,[bool]$expire=$false) {
    $script:snapshots=$inputSnapshots
    $script:observed=0; $script:clockCalls=0; $script:timeout=$expire
    $passed=$true
    try { Wait-NormalExit $name 42 } catch { $passed=$false }
    if ($passed -ne $success -or $script:observed -ne $expectedAudits) {
        throw ('Normal-exit contract failed: '+$name)
    }
    Write-Host ('PASS '+$name)
}
$zero='module_audit holders=0 unavailable=0'
$owned="module_holder pid=42 process=positron.exe module=positron_runtime.dll`nmodule_audit holders=1 unavailable=0"
$foreign="module_holder pid=43 process=positron.exe module=positron_runtime.dll`nmodule_audit holders=1 unavailable=0"
Check 'immediate-exit' @($zero) $true 1
Check 'own-detach-then-exit' @($owned,$owned,$zero) $true 3
Check 'foreign-holder' @($foreign,$zero) $false 1
Check 'mixed-holders' @(($owned+"`n"+$foreign),$zero) $false 1
Check 'unavailable' @('module_audit holders=0 unavailable=1',$zero) $false 1
Check 'partial-log' @('module_holder pid=42',$zero) $false 1
Check 'missing-log' @('missing',$zero) $false 1
Check 'own-detach-timeout' @($owned,$zero) $false 1 $true
Write-Host 'APP RUNTIME GATE UNIT PASS checks=8 device=none'
