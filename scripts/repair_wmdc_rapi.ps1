param(
    [switch] $Elevated,
    [string] $ResultPath = "",
    [switch] $AuditOnly,
    [switch] $QuietHealthy
)

$ErrorActionPreference = "Stop"
$messages = New-Object "System.Collections.Generic.List[string]"

function Test-IsAdministrator
{
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Complete-Repair([int] $exitCode)
{
    if (![string]::IsNullOrEmpty($ResultPath)) {
        [IO.File]::WriteAllLines($ResultPath, $messages,
                (New-Object Text.UTF8Encoding($false)))
    } else {
        foreach ($message in $messages) {
            Write-Host $message
        }
    }
    exit $exitCode
}

$components = @(
    [pscustomobject]@{
        Clsid = "{412E4330-A2EA-4A1C-B723-873E5FA1C618}"
        File = "wcescommproxy.dll"
    },
    [pscustomobject]@{
        Clsid = "{35440327-1517-4B72-865E-3FFE8E97002F}"
        File = "rapistub.dll"
    },
    [pscustomobject]@{
        Clsid = "{499C0C20-A766-11CF-8011-00A0C90A8F78}"
        File = "rapi.dll"
    },
    [pscustomobject]@{
        Clsid = "{DCBEB807-14D0-4CBD-926C-B991F4FD1B91}"
        File = "rapiproxystub.dll"
    },
    [pscustomobject]@{
        Clsid = "{4ED4A55B-629E-4B3D-9F18-86CCE9581262}"
        File = "wcescommproxy.dll"
    }
)
$views = @(
    [pscustomobject]@{
        View = [Microsoft.Win32.RegistryView]::Registry32
        Directory = Join-Path $env:SystemRoot "SysWOW64"
    },
    [pscustomobject]@{
        View = [Microsoft.Win32.RegistryView]::Registry64
        Directory = Join-Path $env:SystemRoot "System32"
    }
)
$plans = New-Object "System.Collections.Generic.List[object]"

try {
    foreach ($view in $views) {
        foreach ($component in $components) {
            $target = Join-Path $view.Directory $component.File
            if (!(Test-Path -LiteralPath $target -PathType Leaf)) {
                throw "Required WMDC file is missing: $target"
            }
            $keyPath = "SOFTWARE\Classes\CLSID\" + $component.Clsid +
                    "\InprocServer32"
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
                    [Microsoft.Win32.RegistryHive]::LocalMachine,
                    $view.View)
            $key = $base.OpenSubKey($keyPath)
            if ($null -eq $key) {
                $base.Close()
                throw "Required WMDC COM key is missing: HKLM\$keyPath"
            }
            $current = [string] $key.GetValue("", $null,
                    [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
            $kind = $key.GetValueKind("")
            $key.Close()
            $base.Close()
            $legacy = "%windir%\system32\" + $component.File
            if (![string]::Equals($current, $target,
                    [StringComparison]::OrdinalIgnoreCase) -and
                    ![string]::Equals($current, $legacy,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing unexpected COM value for $($component.Clsid): $current"
            }
            $plans.Add([pscustomobject]@{
                View = $view.View
                KeyPath = $keyPath
                Clsid = $component.Clsid
                Target = $target
                Current = $current
                Kind = $kind
                NeedsChange = ![string]::Equals($current, $target,
                        [StringComparison]::OrdinalIgnoreCase) -or
                        $kind -ne [Microsoft.Win32.RegistryValueKind]::String
            })
        }
    }
} catch {
    $messages.Add("status=FAIL")
    $messages.Add("error=$($_.Exception.Message)")
    Complete-Repair 1
}

$needed = @($plans | Where-Object { $_.NeedsChange })
if ($needed.Count -eq 0) {
    if (!$QuietHealthy) {
        foreach ($plan in $plans) {
            $messages.Add("already[$($plan.View),$($plan.Clsid)]=$($plan.Target)")
        }
        $messages.Add("changed=0")
        $messages.Add("status=PASS")
    }
    Complete-Repair 0
}

if ($AuditOnly) {
    foreach ($plan in $needed) {
        $messages.Add("needs-repair[$($plan.View),$($plan.Clsid)]=$($plan.Current)")
    }
    $messages.Add("required=$($needed.Count)")
    $messages.Add("status=REPAIR_REQUIRED")
    Complete-Repair 2
}

if (!(Test-IsAdministrator)) {
    if ($Elevated) {
        $messages.Add("status=FAIL")
        $messages.Add("error=UAC elevation did not produce an administrator token.")
        Complete-Repair 1
    }
    Write-Host "[wmdc-rapi] known legacy COM registration requires repair; approve UAC to continue."
    $childName = "Positron-wmdc-rapi-" +
            [Guid]::NewGuid().ToString("N") + ".txt"
    $childResult = Join-Path ([IO.Path]::GetTempPath()) $childName
    $powerShell = Join-Path $PSHOME "powershell.exe"
    $arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass " +
            "-File `"$PSCommandPath`" -Elevated " +
            "-ResultPath `"$childResult`""
    try {
        $process = Start-Process -FilePath $powerShell -Verb RunAs `
                -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
        if (Test-Path -LiteralPath $childResult) {
            Get-Content -LiteralPath $childResult -Encoding UTF8
            Remove-Item -LiteralPath $childResult -Force
        } else {
            Write-Error "The elevated repair did not produce a result file."
        }
        exit $process.ExitCode
    } catch {
        if (Test-Path -LiteralPath $childResult) {
            Remove-Item -LiteralPath $childResult -Force
        }
        throw
    }
}

try {
    $changed = New-Object "System.Collections.Generic.List[object]"
    try {
        foreach ($plan in $plans) {
            if (!$plan.NeedsChange) {
                $messages.Add("already[$($plan.View),$($plan.Clsid)]=$($plan.Target)")
                continue
            }
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
                    [Microsoft.Win32.RegistryHive]::LocalMachine,
                    $plan.View)
            $key = $base.OpenSubKey($plan.KeyPath, $true)
            $key.SetValue("", $plan.Target,
                    [Microsoft.Win32.RegistryValueKind]::String)
            $key.Close()
            $base.Close()
            $changed.Add($plan)
            $messages.Add("fixed[$($plan.View),$($plan.Clsid)]=$($plan.Target)")
        }
    } catch {
        foreach ($plan in $changed) {
            $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
                    [Microsoft.Win32.RegistryHive]::LocalMachine,
                    $plan.View)
            $key = $base.OpenSubKey($plan.KeyPath, $true)
            $key.SetValue("", $plan.Current, $plan.Kind)
            $key.Close()
            $base.Close()
        }
        throw
    }
    $messages.Add("changed=$($changed.Count)")
    $messages.Add("status=PASS")
    Complete-Repair 0
} catch {
    $messages.Add("status=FAIL")
    $messages.Add("error=$($_.Exception.Message)")
    Complete-Repair 1
}
