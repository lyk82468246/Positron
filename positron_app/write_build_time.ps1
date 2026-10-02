param([Parameter(Mandatory = $true)][string] $OutputDirectory)

$ErrorActionPreference = 'Stop'
$output = [IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($output)
# VS runs this before compilation, not when the application starts. One stamp
# per Debug project build, including incremental builds of other app sources.
$stamp = [DateTime]::Now.ToString('yyyy-MM-dd HH:mm:ss',
        [Globalization.CultureInfo]::InvariantCulture)
$header = "/* Generated Debug build identity; do not commit. */`r`n" +
        "#define APP_DEBUG_BUILD_TIME `"$stamp`"`r`n"
[IO.File]::WriteAllText((Join-Path $output 'app_build_time.h'), $header,
        [Text.Encoding]::ASCII)
