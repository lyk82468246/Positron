$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $PSScriptRoot 'app_settings_live_gate.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Live gate parse failure.' }
$fn = $ast.Find({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Test-LiveExitPending'
}, $false)
if ($null -eq $fn) { throw 'Missing pending-exit guard.' }
. ([scriptblock]::Create($fn.Extent.Text))
$holder = "module_holder pid=123 process=positron.exe module=positron_core.dll path=`r`n"
$summary = "module_audit holders=1 unavailable=0`r`n"
$cases = @(
    @(($holder + $summary), 123, $true),
    @(($holder + $summary), 124, $false),
    @(($holder + $summary), 0, $false),
    @(($holder + "module_audit holders=1 unavailable=1`r`n"), 123, $false),
    @(($holder + "module_audit holders=2 unavailable=0`r`n"), 123, $false),
    @($holder, 123, $false),
    @(($holder + $summary + $summary), 123, $false),
    @($summary, 123, $false),
    @(($holder + "module_holder pid=124 process=test_host.exe module=positron_core.dll path=`r`n" + $summary), 123, $false)
)
foreach ($case in $cases) {
    if ((Test-LiveExitPending $case[0] $case[1]) -ne $case[2]) {
        throw ('Incorrect exit guard: ' + $case[0])
    }
}
Write-Host ('Settings live exit guard PASS cases=' + $cases.Count)

# Exercise the real wait adapter without any RAPI connection or file writes.
Add-Type -TypeDefinition 'public static class PositronDeviceRapi { public static void DeleteFileIfExists(string path) {} }'
$wait = $ast.Find({param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Wait-LiveExit'
}, $false)
if ($null -eq $wait) { throw 'Missing exit wait.' }
. ([scriptblock]::Create($wait.Extent.Text))
$remote = '\fixture'
$evidence = $PSScriptRoot
function Get-Content { param($LiteralPath, [switch]$Raw, $Encoding) return $script:auditBody }
function Invoke-RemoteModuleAudit {
    param($Executable, $RemoteLog, $LocalLog, $TimeoutSeconds)
    ++$script:calls
    if ($script:calls -eq 1) { throw $script:auditError }
}
$script:calls = 0
$script:auditBody = $holder + $summary
$script:auditError = 'The device module audit did not confirm an empty holder set'
Wait-LiveExit 'create' 123
if ($script:calls -ne 2) { throw 'Did not wait for own process to exit.' }
foreach ($failure in @(
        @('The device module audit did not confirm an empty holder set', ($holder + $summary), 124),
        @('The device module audit did not confirm an empty holder set', ($holder + "module_audit holders=1 unavailable=1`r`n"), 123),
        @('RAPI connection failed', ($holder + $summary), 123))) {
    $script:calls = 0
    $script:auditError = $failure[0]
    $script:auditBody = $failure[1]
    $rejected = $false
    try { Wait-LiveExit 'create' $failure[2] } catch { $rejected = $true }
    if (!$rejected -or $script:calls -ne 1) { throw 'Unexpected audit failure was retried.' }
}
Write-Host 'Settings live wait PASS cases=4'
