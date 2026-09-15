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
    $selectionFunction = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-RemoteBaseSelection'
    }, $false)
    if ($null -eq $selectionFunction) {
        throw 'Cannot locate the remote-base selection policy.'
    }
    . ([scriptblock]::Create($selectionFunction.Extent.Text))
    $storageCases = @(
        @{Automatic=$true; Available=$true; PathSpecific=$true; Free=200; Required=100; Expected='external'},
        @{Automatic=$true; Available=$false; PathSpecific=$false; Free=200; Required=100; Expected='internal_fallback'},
        @{Automatic=$true; Available=$true; PathSpecific=$false; Free=200; Required=100; Expected='internal_fallback'},
        @{Automatic=$true; Available=$true; PathSpecific=$true; Free=99; Required=100; Expected='internal_fallback'},
        @{Automatic=$false; Available=$false; PathSpecific=$false; Free=0; Required=100; Expected='explicit'}
    )
    foreach ($case in $storageCases) {
        $actual = Get-RemoteBaseSelection $case.Automatic $case.Available `
                $case.PathSpecific $case.Free $case.Required
        if ($actual -ne $case.Expected) {
            throw "Remote-base selection mismatch: expected=$($case.Expected); actual=$actual"
        }
    }
    $pathsFunction = $ast.Find({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Get-RemoteGatePaths'
    }, $false)
    if ($null -eq $pathsFunction) {
        throw 'Cannot locate the remote gate path builder.'
    }
    . ([scriptblock]::Create($pathsFunction.Extent.Text))
    $preferredPaths = Get-RemoteGatePaths `
            '\Storage Card\Temp\Positron-device-gate' 'next816' `
            '20260915-120000' 'test_host-run-20260915-120000.exe'
    if ($preferredPaths.OwnerRoot -ne '\Storage Card\Temp\Positron-device-gate' -or
            $preferredPaths.Root -ne '\Storage Card\Temp\Positron-device-gate\next816-20260915-120000' -or
            $preferredPaths.Exe -notmatch '\\test_host-run-20260915-120000\.exe$') {
        throw 'Preferred external remote path layout changed unexpectedly.'
    }
    $gateText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'device_gate.ps1') `
            -Raw -Encoding UTF8
    if ($gateText -notmatch '\[string\]\s+\$RemoteBase\s*=\s*""') {
        throw 'Automatic remote-base mode is no longer the device-gate default.'
    }
    Write-Output ("Device gate configuration: {0} cases; storage policy: {1} cases; path layout: PASS." -f
            $cases.Count, $storageCases.Count)
} finally {
    if (Test-Path -LiteralPath $ini) { Remove-Item -LiteralPath $ini }
    Remove-Item -LiteralPath $temp
}
