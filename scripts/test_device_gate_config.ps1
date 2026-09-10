$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot 'device_gate.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$function = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-ConfiguredTests'
}, $false)
# Load only the pure configuration validator, never the deployment body.
. ([scriptblock]::Create($function.Extent.Text))
$temp = Join-Path ([IO.Path]::GetTempPath()) ('positron-config-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($temp)
$ini = Join-Path $temp 'test_host.ini'
try {
    $cases = @(
        @{Text="auto=1`ntests=1-22,24-45,62,118,999"; Count=47},
        @{Text="tests=118,7b,999 # comment`nauto=1"; Count=3},
        @{Text="auto=1`ntests=1-45,62,118,999"; Count=-1},
        @{Text="auto=1`ntests=78"; Count=-1},
        @{Text="auto=1`ntests=998-1000"; Count=-1},
        @{Text="auto=1`ntests=0"; Count=-1},
        @{Text="auto=1`ntests=999999"; Count=-1},
        @{Text="auto=1`ntests=1-2147483647"; Count=-1},
        @{Text="auto=1`ntests=4-2"; Count=-1},
        @{Text="auto=0`ntests=999"; Count=-1},
        @{Text="tests=999"; Count=-1},
        @{Text="auto=1`nauto=1`ntests=999"; Count=-1},
        @{Text="auto=1`ntests=999`ntests=118"; Count=-1}
    )
    foreach ($case in $cases) {
        [IO.File]::WriteAllText($ini, $case.Text, [Text.UTF8Encoding]::new($false))
        $rejected = $false
        try { $actual = @(Get-ConfiguredTests $ini) } catch { $rejected = $true }
        if ($case.Count -lt 0) {
            if (!$rejected) { throw "Invalid configuration accepted: $($case.Text)" }
        } elseif ($rejected -or $actual.Count -ne $case.Count) {
            throw "Valid configuration failed: $($case.Text); count=$($actual.Count)"
        }
    }
    Write-Output "Device gate configuration: $($cases.Count) cases passed."
} finally {
    if (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    Remove-Item -LiteralPath $temp
}
