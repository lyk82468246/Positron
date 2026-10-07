# VS2008 does not reliably track files named by RC RCDATA declarations.
# Invalidate only this project's generated resource object when inputs changed;
# the formal resource compiler/linker still create all product artifacts.
param([Parameter(Mandatory=$true)][string] $OutputDirectory)
$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
$debug = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'bin\Debug'))
$release = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'bin\Release'))
if ($output -ne $debug -and $output -ne $release) {
    throw 'Resource output must be this project bin/Debug or bin/Release.'
}
$resource = Join-Path $output 'positron_app.res'
if (!(Test-Path -LiteralPath $resource)) { exit 0 }
$stamp = (Get-Item -LiteralPath $resource).LastWriteTimeUtc
$inputs = @((Join-Path $PSScriptRoot 'positron_app.rc'),
    (Join-Path $PSScriptRoot 'resource.h'))
foreach ($language in @('en-US', 'zh-CN')) {
    foreach ($page in @('welcome', 'controls', 'about', 'newtab', 'history', 'downloads', 'settings')) {
        $inputs += Join-Path $PSScriptRoot ('resources\' + $language + '\' + $page + '.html')
    }
}
foreach ($inputPath in $inputs) {
    if ((Get-Item -LiteralPath $inputPath).LastWriteTimeUtc -gt $stamp) {
        Remove-Item -LiteralPath $resource
        Write-Host 'HTML/RC input changed; formal build will recompile embedded resources.'
        exit 0
    }
}
