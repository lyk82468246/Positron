$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot 'device_gate.ps1'), [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
$auditFunction = $ast.Find({ param($node)
    $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Invoke-RemoteModuleAudit'
}, $false)
if ($null -eq $auditFunction) { throw 'Missing guest module audit runner.' }
# Run the actual wait/validation function against synthetic RAPI snapshots.
# The deployment body and real device APIs are never loaded in this test.
. ([scriptblock]::Create($auditFunction.Extent.Text))
function Write-Stage([string] $message) { }
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class PositronDeviceRapi
{
    public static string[] Snapshots;
    public static int Reads;
    public static int Launches;
    public static void Reset(string[] snapshots)
    {
        Snapshots = snapshots;
        Reads = 0;
        Launches = 0;
    }
    public static uint LaunchProcess(string image, string directory, string args)
    {
        if (args != "--audit-modules") throw new Exception("Unexpected audit mode.");
        Launches++;
        return 42;
    }
    public static bool TryCopyFileFromDevice(string remote, string local)
    {
        string text = Snapshots[Math.Min(Reads++, Snapshots.Length - 1)];
        if (text == "<unavailable>") return false;
        File.WriteAllText(local, text, new UTF8Encoding(false));
        return true;
    }
}
'@
$temp = Join-Path ([IO.Path]::GetTempPath()) ('positron-audit-' + [guid]::NewGuid())
[void][IO.Directory]::CreateDirectory($temp)
$localLog = Join-Path $temp 'module-audit.log'
$pass = "Positron module audit v1`r`nmodule_audit holders=0 unavailable=0`r`n"
$cases = @(
    @{Name='complete'; Snapshots=@($pass); Error=''; Reads=1},
    @{Name='empty then partial then complete'; Snapshots=@('', 'Positron module audit v1', $pass); Error=''; Reads=3},
    @{Name='read unavailable then complete'; Snapshots=@('<unavailable>', $pass); Error=''; Reads=2},
    @{Name='holder present'; Snapshots=@('module_audit holders=1 unavailable=0'); Error='empty holder set'; Reads=1},
    @{Name='snapshot unavailable'; Snapshots=@('module_audit holders=0 unavailable=1'); Error='empty holder set'; Reads=1},
    @{Name='empty until deadline'; Snapshots=@(''); Error='complete summary'; Reads=0},
    @{Name='partial until deadline'; Snapshots=@('module_audit holders=0'); Error='complete summary'; Reads=0}
)
try {
    foreach ($case in $cases) {
        if (Test-Path -LiteralPath $localLog) { Remove-Item -LiteralPath $localLog }
        [PositronDeviceRapi]::Reset([string[]]$case.Snapshots)
        $failure = ''
        $result = $null
        try {
            $result = Invoke-RemoteModuleAudit '\test\helper.exe' `
                    '\test\module-audit.log' $localLog 1
        } catch { $failure = $_.Exception.Message }
        if ($case.Error -eq '') {
            if ($failure -ne '' -or $null -eq $result -or
                    $result.Summary -ne 'module_audit holders=0 unavailable=0' -or
                    !$result.LogRetrieved -or $result.ProcessId -ne 42) {
                throw "Audit did not confirm the complete snapshot: $($case.Name); $failure"
            }
        } elseif ($failure -notlike ('*' + $case.Error + '*')) {
            throw "Audit did not fail closed: $($case.Name); $failure"
        }
        if ([PositronDeviceRapi]::Launches -ne 1 -or
                ($case.Reads -gt 0 -and [PositronDeviceRapi]::Reads -ne $case.Reads)) {
            throw "Audit restarted or skipped snapshots: $($case.Name)"
        }
    }
    Write-Output ("Guest module audit wait: {0} cases PASS (synthetic RAPI only)." -f $cases.Count)
} finally {
    if (Test-Path -LiteralPath $localLog) { Remove-Item -LiteralPath $localLog }
    Remove-Item -LiteralPath $temp
}
