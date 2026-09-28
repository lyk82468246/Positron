[CmdletBinding()]
param(
    [string]$Repository,
    [ValidateRange(0, 99)]
    [int]$BuildNumber = 1,
    [string]$OutputDirectory,
    [switch]$SkipUpload,
    [switch]$SkipSourceBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$cabProject = Join-Path $root "positron_cab"
$cabRelease = Join-Path $cabProject "Release"
$cabSource = Join-Path $cabProject "cab-source"
$defaultOutput = Join-Path $root "tmp\nightly-cab"
if ([string]::IsNullOrEmpty($OutputDirectory)) {
    $OutputDirectory = $defaultOutput
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$finalName = "positron-nightly-cab-wm6-armv4i.cab"
$finalCab = Join-Path $output $finalName
$finalReadme = Join-Path $output "NIGHTLY-CAB-README.md"
$finalSums = Join-Path $output "SHA256SUMS.txt"
$tag = "nightly-cab"

function Fail([string]$message) {
    throw $message
}

function Require-Command([string]$name) {
    $command = Get-Command $name -ErrorAction SilentlyContinue
    if ($null -eq $command) {
        Fail "未找到命令：$name"
    }
    return $command.Source
}

function Assert-InfContains([string]$text, [string]$pattern, [string]$description) {
    if (-not [regex]::IsMatch($text, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
        Fail "CAB INF 缺少或不匹配：$description"
    }
}

function Assert-InfFile([string]$infPath, [string]$version, [string]$buildDate) {
    $text = [IO.File]::ReadAllText($infPath)
    Assert-InfContains $text "(?m)^\s*ProcessorType\s*=\s*(?:2577|ARMV4I)\s*$" "ARMV4I / ProcessorType=2577"
    Assert-InfContains $text "(?m)^\s*VersionMin\s*=\s*5\.02\s*$" "VersionMin=5.02"
    Assert-InfContains $text "(?m)^\s*VersionMax\s*=\s*6\.99\s*$" "VersionMax=6.99（VS2008 合法上限）"
    Assert-InfContains $text "%InstallDir%" "应用安装目录"
    Assert-InfContains $text "%CE2%" "Windows 目录"
    Assert-InfContains $text "%CE2%\\fonts" "Windows Fonts 目录"
    Assert-InfContains $text "%CE11%" "Start Menu Programs 目录"
    Assert-InfContains $text "licenses" "许可证目录"
    Assert-InfContains $text "positron\.exe" "positron.exe"

    foreach ($name in @(
        "positron_tls\.dll", "positron_json\.dll", "positron_http\.dll",
        "positron_core\.dll", "positron_image\.dll", "positron_script\.dll",
        "positron_browser\.dll", "PositronSymbolsBasic\.ttf",
        "PositronSymbols\.ttf", "PositronEmoji\.ttf", "LICENSE",
        "THIRD_PARTY\.md", "OFL-NotoSymbols\.txt", "OFL-NotoSymbols2\.txt",
        "OFL-NotoEmoji\.txt"
    )) {
        Assert-InfContains $text $name $name
    }

    Assert-InfContains $text "(?m)^\s*\[Shortcuts\]\s*$" "CEShortcuts 快捷方式段"
    Assert-InfContains $text '(?m)^\s*"Positron",0,"positron\.exe","%CE11%"\s*$' "Positron 快捷方式"
    Assert-InfContains $text '(?m)^\s*"HKLM","Software\\Positron"' 'HKLM\Software\Positron'
    Assert-InfContains $text "Software\\Positron" "Software\Positron"
    Assert-InfContains $text "Version.*$([regex]::Escape($version))" "Version=$version"
    Assert-InfContains $text "BuildDate.*$([regex]::Escape($buildDate))" "BuildDate=$buildDate"
    Assert-InfContains $text "Channel.*nightly-cab" "Channel=nightly-cab"
    Assert-InfContains $text "InstallDir.*Program Files\\Positron" "InstallDir"

    foreach ($forbidden in @("test_host", "positron_media\.dll", "fixtures", "\.pdb", "\.lib")) {
        if ([regex]::IsMatch($text, $forbidden, [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            Fail "CAB INF 包含禁止内容：$forbidden"
        }
    }
    return $text
}

function Get-Sha256Hex([string]$path) {
    $algorithm = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($path)
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace("-", "").ToLowerInvariant()
    }
    finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Prepare-CabSources {
    if (-not (Test-Path -LiteralPath $cabSource)) {
        New-Item -ItemType Directory -Path $cabSource -Force | Out-Null
    }
    $licenseSources = @(
        @{ Source = "third_party\noto-symbols\OFL.txt"; Name = "OFL-NotoSymbols.txt" },
        @{ Source = "third_party\noto-symbols2\OFL.txt"; Name = "OFL-NotoSymbols2.txt" },
        @{ Source = "third_party\noto-emoji\OFL.txt"; Name = "OFL-NotoEmoji.txt" }
    )
    foreach ($mapping in $licenseSources) {
        $source = Join-Path $root $mapping.Source
        if (-not (Test-Path -LiteralPath $source)) {
            Fail "缺少字体许可证源文件：$source"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $cabSource $mapping.Name) -Force
    }
}

if (-not (Test-Path -LiteralPath $output)) {
    New-Item -ItemType Directory -Path $output -Force | Out-Null
}

Prepare-CabSources

if (-not $SkipSourceBuild) {
    $buildScript = Join-Path $root "scripts\build.bat"
    Write-Host "运行 Release|Windows Mobile 6 Professional SDK (ARMV4I) 源码构建..."
    & $buildScript Release rebuild
    $sourceBuildExitCode = $LASTEXITCODE
    if ($sourceBuildExitCode -ne 0) {
        $recoveryBuildLimit = 4
        for ($recoveryBuild = 1; $recoveryBuild -le $recoveryBuildLimit; $recoveryBuild++) {
            Write-Host "Rebuild 未成功；执行第 $recoveryBuild/$recoveryBuildLimit 次普通 Release Build 以收敛 VS2008 并行依赖..."
            & $buildScript Release build
            $sourceBuildExitCode = $LASTEXITCODE
            if ($sourceBuildExitCode -eq 0) {
                break
            }
        }
        if ($sourceBuildExitCode -ne 0) {
            Fail "源码构建重试失败（已执行 $recoveryBuildLimit 次普通 Build），退出码：$sourceBuildExitCode"
        }
    }
}

$infCandidates = @(Get-ChildItem -LiteralPath $cabRelease -Filter "*.inf" -File -ErrorAction SilentlyContinue)
if ($infCandidates.Count -eq 0) {
    Fail "未找到 $cabRelease 中的 INF。请在 VS2008 GUI 中选择 Release，右键 positron_cab 项目执行 Build，然后重新运行；若已完成源码构建，可使用 -SkipSourceBuild。"
}
$inf = $infCandidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1

$today = Get-Date
$buildDate = $today.ToString("yyyy-MM-dd", [Globalization.CultureInfo]::InvariantCulture)
$version = "{0}.{1}.{2}.{3:D2}" -f $today.ToString("yyyy"), $today.ToString("MM"), $today.ToString("dd"), $BuildNumber

$infText = [IO.File]::ReadAllText($inf.FullName)
if ([regex]::Matches($infText, [regex]::Escape("__POSITRON_CAB_VERSION__")).Count -ne 1) {
    Fail "INF 中 Version 占位符数量不是 1：$($inf.FullName)"
}
if ([regex]::Matches($infText, [regex]::Escape("__POSITRON_CAB_BUILD_DATE__")).Count -ne 1) {
    Fail "INF 中 BuildDate 占位符数量不是 1：$($inf.FullName)"
}
$infText = $infText.Replace("__POSITRON_CAB_VERSION__", $version)
$infText = $infText.Replace("__POSITRON_CAB_BUILD_DATE__", $buildDate)
if ([regex]::Matches($infText, "(?im)^\s*ProcessorType\s*=").Count -ne 0) {
    Fail "INF 已经包含 ProcessorType，无法安全注入 ARMV4I 目标"
}
if ([regex]::Matches($infText, "(?im)^\s*VersionMax\s*=\s*6\.99\s*$").Count -ne 1) {
    Fail "INF 中 VersionMax=6.99 位置不是 1 个，无法注入 ARMV4I 目标"
}
$infText = $infText.Replace("VersionMax=6.99", "VersionMax=6.99" + [Environment]::NewLine + "ProcessorType=2577")

$stagedInf = Join-Path $output "positron-nightly-cab-wm6-armv4i.inf"
$cabwizError = Join-Path $output "cabwiz.err"
[IO.File]::WriteAllText($stagedInf, $infText, [Text.Encoding]::Unicode)
Assert-InfFile $stagedInf $version $buildDate | Out-Null

$programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
$cabwizCandidates = @(
    (Join-Path $programFilesX86 "Microsoft Visual Studio 9.0\SmartDevices\SDK\SDKTools\cabwiz.exe"),
    (Join-Path $env:ProgramFiles "Microsoft Visual Studio 9.0\SmartDevices\SDK\SDKTools\cabwiz.exe"),
    (Join-Path $programFilesX86 "Windows Mobile 6 SDK\Tools\CabWiz\cabwiz.exe"),
    (Join-Path $env:ProgramFiles "Windows Mobile 6 SDK\Tools\CabWiz\cabwiz.exe")
)
$cabwiz = $cabwizCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ([string]::IsNullOrEmpty($cabwiz)) {
    Fail "未找到 VS2008/WM6 SDK cabwiz.exe"
}

Write-Host "运行 CabWiz..."
$cabwizOutput = Join-Path $output ".cabwiz"
if (Test-Path -LiteralPath $cabwizOutput) {
    Remove-Item -LiteralPath $cabwizOutput -Recurse -Force
}
New-Item -ItemType Directory -Path $cabwizOutput -Force | Out-Null
& $cabwiz $stagedInf /dest $cabwizOutput /err $cabwizError /compress
if ($LASTEXITCODE -ne 0) {
    $detail = if (Test-Path -LiteralPath $cabwizError) { [IO.File]::ReadAllText($cabwizError) } else { "" }
    Fail "CabWiz 失败。$detail"
}
$cabwizCabName = [IO.Path]::GetFileNameWithoutExtension($stagedInf) + ".CAB"
$cabwizCab = Join-Path $cabwizOutput $cabwizCabName
if (-not (Test-Path -LiteralPath $cabwizCab)) {
    Fail "CabWiz 未生成预期 CAB：$cabwizCab"
}
$generatedCab = $finalCab
if (Test-Path -LiteralPath $generatedCab) {
    Remove-Item -LiteralPath $generatedCab -Force
}
Move-Item -LiteralPath $cabwizCab -Destination $generatedCab -Force
Remove-Item -LiteralPath $cabwizOutput -Recurse -Force
$cabwizCab = $null
if (Test-Path -LiteralPath $cabwizError) {
    Remove-Item -LiteralPath $cabwizError -Force
}
$magic = [IO.File]::ReadAllBytes($generatedCab)[0..3] -join ","
if ($magic -ne "77,83,67,70") {
    Fail "生成文件不是 MSCF CAB：$generatedCab"
}

$expand = Join-Path $env:SystemRoot "System32\expand.exe"
if (Test-Path -LiteralPath $expand) {
    $listing = @(& $expand -D $generatedCab 2>&1)
    if ($LASTEXITCODE -ne 0) {
        Fail "expand.exe 无法读取 CAB 内容"
    }
    foreach ($forbidden in @("test_host", "positron_media\.dll", "fixtures", "\.pdb", "\.lib")) {
        if ([regex]::IsMatch(($listing -join [Environment]::NewLine), $forbidden, [Text.RegularExpressions.RegexOptions]::IgnoreCase)) {
            Fail "CAB 内容包含禁止文件：$forbidden"
        }
    }
}

$commit = (& git -C $root rev-parse HEAD).Trim()
$dirty = (& git -C $root status --porcelain).Trim()
if ([string]::IsNullOrEmpty($dirty)) {
    $state = "clean"
}
else {
    $state = "dirty (pre-existing changes may be present)"
}
$readme = @(
    "# Positron nightly CAB",
    "",
    "- Asset: $finalName",
    "- Channel/tag: $tag",
    "- Commit: $commit",
    "- Version: $version",
    "- BuildDate: $buildDate",
    "- Worktree at packaging time: $state",
    "- Target: Windows Mobile 6 Professional / ARMV4I",
    "",
    "The CAB is built from the VS2008 Smart Device deployment project. The application",
    "is installed under \Program Files\Positron, public DLLs under \Windows,",
    "fonts under \Windows\Fonts, and the Start Menu shortcut under",
    "\Windows\Start Menu\Programs.",
    "",
    "The CAB deliberately excludes positron_media.dll, test_host.exe, fixtures,",
    "debug symbols, import libraries, and source files. Install, upgrade, and",
    "uninstall still require validation on a clean WM6 Professional ARMV4I device."
) -join [Environment]::NewLine
[IO.File]::WriteAllText($finalReadme, $readme, (New-Object Text.UTF8Encoding($false)))

$hash = Get-Sha256Hex $generatedCab
[IO.File]::WriteAllText($finalSums, ($hash + "  " + $finalName + [Environment]::NewLine), (New-Object Text.UTF8Encoding($false)))

if (-not $SkipUpload) {
    $gh = Require-Command "gh"
    $repoArgs = @()
    if (-not [string]::IsNullOrEmpty($Repository)) {
        $repoArgs = @("--repo", $Repository)
    }
    & git -C $root tag -f $tag $commit
    if ($LASTEXITCODE -ne 0) { Fail "更新本地 $tag tag 失败" }
    & git -C $root push origin "+refs/tags/${tag}:refs/tags/${tag}"
    if ($LASTEXITCODE -ne 0) { Fail "推送 $tag 失败" }
    & $gh release view $tag @repoArgs *> $null
    if ($LASTEXITCODE -ne 0) {
        & $gh release create $tag $generatedCab $finalReadme $finalSums @repoArgs --title "Positron nightly CAB" --notes "Rolling nightly CAB for WM6 ARMV4I." --prerelease
    }
    else {
        & $gh release upload $tag $generatedCab $finalReadme $finalSums @repoArgs --clobber
    }
    if ($LASTEXITCODE -ne 0) { Fail "nightly-cab release 上传失败" }
}

Write-Host "CAB 完成：$generatedCab"
Write-Host "README：$finalReadme"
Write-Host "SHA256：$finalSums"
