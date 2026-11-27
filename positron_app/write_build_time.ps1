param(
    [Parameter(Mandatory = $true)][string] $OutputDirectory,
    [switch] $IfDirty,
    [switch] $IfMissing
)

$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($output)
$headerPath = Join-Path $output 'app_build_time.h'
if ($IfMissing -and (Test-Path -LiteralPath $headerPath)) {
    exit 0
}
if ($IfDirty -and (Test-Path -LiteralPath $headerPath)) {
    $versionObj = Join-Path $output 'app_version.obj'
    $exePath = Join-Path $output 'positron.exe'
    if ((Test-Path -LiteralPath $versionObj) -and (Test-Path -LiteralPath $exePath)) {
        $headerTime = (Get-Item -LiteralPath $headerPath).LastWriteTimeUtc
        $appDir = $PSScriptRoot
        $repoRoot = Split-Path -Parent $appDir
        $dirty = $false
        foreach ($item in Get-ChildItem -LiteralPath $appDir -File) {
            if ($item.Extension -in @('.c', '.h', '.rc', '.ps1') -and $item.LastWriteTimeUtc -gt $headerTime) {
                $dirty = $true
                break
            }
        }
        if (-not $dirty) {
            $resDir = Join-Path $appDir 'resources'
            if (Test-Path -LiteralPath $resDir) {
                foreach ($item in Get-ChildItem -LiteralPath $resDir -File -Recurse) {
                    if ($item.LastWriteTimeUtc -gt $headerTime) {
                        $dirty = $true
                        break
                    }
                }
            }
        }
        if (-not $dirty) {
            foreach ($libDir in @('positron_archive','positron_runtime','positron_core','positron_browser','positron_http','positron_script','positron_json')) {
                $libPath = Join-Path $repoRoot ($libDir + '\bin\Debug\' + $libDir + '.lib')
                if ((Test-Path -LiteralPath $libPath) -and (Get-Item -LiteralPath $libPath).LastWriteTimeUtc -gt $headerTime) {
                    $dirty = $true
                    break
                }
            }
        }
        if (-not $dirty) {
            exit 0
        }
    }
}
# Written before VS checks app_version.c dependencies so incremental Debug builds
# recompile app_version.obj in the same build rather than one build behind.
$stamp = [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss',
        [Globalization.CultureInfo]::InvariantCulture)
$header = "/* Generated Debug build identity; do not commit. */`r`n" +
        "#define APP_DEBUG_BUILD_TIME `"$stamp`"`r`n"
[IO.File]::WriteAllText($headerPath, $header, [Text.Encoding]::ASCII)
