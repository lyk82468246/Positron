[CmdletBinding()]
param(
    [string]$Repository,
    [ValidateRange(0, 99)]
    [int]$BuildNumber = 1,
    [string]$OutputDirectory,
    [switch]$SkipUpload,
    [switch]$SkipSourceBuild,
    [switch]$SkipCabBuild
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$solution = Join-Path $root "Positron.sln"
$buildScript = Join-Path $root "scripts\build.bat"
$cabProject = Join-Path $root "positron_cab"
$cabProjectFile = Join-Path $cabProject "positron_cab.vddproj"
$cabRelease = Join-Path $cabProject "Release"
$cabSource = Join-Path $cabProject "cab-source"
$cabBuildLog = Join-Path $root "vs2008-cab-build.log"
$defaultOutput = Join-Path $root "tmp\nightly-cab"
if ([string]::IsNullOrEmpty($OutputDirectory)) {
    $OutputDirectory = $defaultOutput
}
$output = [IO.Path]::GetFullPath($OutputDirectory)
$finalName = "positron-nightly-cab-wm6-armv4i.cab"
$finalCab = Join-Path $output $finalName
$legacyReadme = Join-Path $output "NIGHTLY-CAB-README.md"
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
    $processorMatches = [regex]::Matches($text, "(?im)^\s*ProcessorType\s*=\s*(\S+)\s*$")
    if ($processorMatches.Count -gt 0 -and $processorMatches[0].Groups[1].Value -notmatch "^(?:2577|ARMV4I)$") {
        Fail "CAB INF 的 ProcessorType 不是 ARMV4I / 2577"
    }
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

    foreach ($forbidden in @("test_host", "positron_db\.dll", "positron_media\.dll", "fixtures", "\.pdb", "\.lib")) {
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

function Find-Devenv {
    $candidates = @()
    if (-not [string]::IsNullOrEmpty($env:VS90COMNTOOLS)) {
        $candidates += (Join-Path $env:VS90COMNTOOLS "..\IDE\devenv.com")
    }
    $candidates += (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio 9.0\Common7\IDE\devenv.com")
    $candidates += (Join-Path $env:ProgramFiles "Microsoft Visual Studio 9.0\Common7\IDE\devenv.com")
    $devenv = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if ([string]::IsNullOrEmpty($devenv)) {
        Fail "未找到 VS2008 devenv.com"
    }
    return $devenv
}

function Assert-CabInputs {
    $required = @(
        (Join-Path $root "LICENSE"),
        (Join-Path $root "THIRD_PARTY.md"),
        (Join-Path $cabSource "OFL-NotoSymbols.txt"),
        (Join-Path $cabSource "OFL-NotoSymbols2.txt"),
        (Join-Path $cabSource "OFL-NotoEmoji.txt"),
        (Join-Path $root "assets\fonts\PositronSymbolsBasic.ttf"),
        (Join-Path $root "assets\fonts\PositronSymbols.ttf"),
        (Join-Path $root "assets\fonts\PositronEmoji.ttf"),
        (Join-Path $root "positron_app\bin\Release\positron.exe"),
        (Join-Path $root "positron_tls\bin\Release\positron_tls.dll"),
        (Join-Path $root "positron_json\bin\Release\positron_json.dll"),
        (Join-Path $root "positron_http\bin\Release\positron_http.dll"),
        (Join-Path $root "positron_core\bin\Release\positron_core.dll"),
        (Join-Path $root "positron_image\bin\Release\positron_image.dll"),
        (Join-Path $root "positron_script\bin\Release\positron_script.dll"),
        (Join-Path $root "positron_browser\bin\Release\positron_browser.dll")
    )
    foreach ($path in $required) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Fail "CAB 输入文件不存在；请先完成 Release 增量构建：$path"
        }
    }
}

function Invalidate-CabReleaseOutputs {
    $names = @(
        $finalName,
        [IO.Path]::ChangeExtension($finalName, ".inf"),
        "CabWiz.log"
    )
    foreach ($name in $names) {
        $path = Join-Path $cabRelease $name
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

function Invoke-WithCabVersion([string]$version, [string]$buildDate, [scriptblock]$action) {
    $originalBytes = [IO.File]::ReadAllBytes($cabProjectFile)
    $projectText = [Text.Encoding]::ASCII.GetString($originalBytes)
    if ([regex]::Matches($projectText, [regex]::Escape("__POSITRON_CAB_VERSION__")).Count -ne 1) {
        Fail "VDD 项目中的 Version 占位符数量不是 1：$cabProjectFile"
    }
    if ([regex]::Matches($projectText, [regex]::Escape("__POSITRON_CAB_BUILD_DATE__")).Count -ne 1) {
        Fail "VDD 项目中的 BuildDate 占位符数量不是 1：$cabProjectFile"
    }
    $projectText = $projectText.Replace("__POSITRON_CAB_VERSION__", $version)
    $projectText = $projectText.Replace("__POSITRON_CAB_BUILD_DATE__", $buildDate)
    [IO.File]::WriteAllBytes($cabProjectFile, [Text.Encoding]::ASCII.GetBytes($projectText))
    try {
        & $action
    }
    finally {
        [IO.File]::WriteAllBytes($cabProjectFile, $originalBytes)
    }
}

function Invoke-VsSolutionBuild([string]$version, [string]$buildDate) {
    $recoveryBuildLimit = 4
    $sourceBuildExitCode = 0
    for ($attempt = 0; $attempt -le $recoveryBuildLimit; $attempt++) {
        if ($attempt -eq 0) {
            Write-Host "运行 Release|Windows Mobile 6 Professional SDK (ARMV4I) 全解决方案增量构建（包含 positron_cab）..."
        }
        else {
            Write-Host "全解决方案增量构建未成功；执行第 $attempt/$recoveryBuildLimit 次普通 Build..."
        }
        $script:CabBuildExitCode = 0
        Invalidate-CabReleaseOutputs
        Invoke-WithCabVersion $version $buildDate {
            & $buildScript Release build
            $script:CabBuildExitCode = $LASTEXITCODE
        }
        $sourceBuildExitCode = $script:CabBuildExitCode
        if ($sourceBuildExitCode -eq 0) {
            break
        }
    }
    if ($sourceBuildExitCode -ne 0) {
        Fail "全解决方案 Release 增量构建重试失败（已执行 $recoveryBuildLimit 次普通 Build），退出码：$sourceBuildExitCode"
    }
}

function Invoke-VsCabProject([string]$version, [string]$buildDate) {
    $script:CabBuildExitCode = 0
    Invoke-WithCabVersion $version $buildDate {
        $devenv = Find-Devenv
        Write-Host "使用 VS2008 增量构建 positron_cab 项目（由 VS 内部调用 CabWiz）..."
        & $devenv $solution /Build "Release|Windows Mobile 6 Professional SDK (ARMV4I)" /Project "positron_cab\positron_cab.vddproj" /Out $cabBuildLog
        $script:CabBuildExitCode = $LASTEXITCODE
    }
    if ($script:CabBuildExitCode -ne 0) {
        Fail "VS2008 positron_cab 项目构建失败，退出码：$script:CabBuildExitCode；详见 $cabBuildLog"
    }
}

if (-not (Test-Path -LiteralPath $output)) {
    New-Item -ItemType Directory -Path $output -Force | Out-Null
}

$today = Get-Date
$buildDate = $today.ToString("yyyy-MM-dd", [Globalization.CultureInfo]::InvariantCulture)
$version = "{0}.{1}.{2}.{3:D2}" -f $today.ToString("yyyy"), $today.ToString("MM"), $today.ToString("dd"), $BuildNumber
if (-not $SkipSourceBuild) {
    Invoke-VsSolutionBuild $version $buildDate
}
Assert-CabInputs
if ($SkipCabBuild -and -not $SkipSourceBuild) {
    Fail "当前 Release 全解决方案包含 positron_cab；-SkipCabBuild 只能与 -SkipSourceBuild 一起使用。"
}
if ($SkipSourceBuild -and -not $SkipCabBuild) {
    Invoke-VsCabProject $version $buildDate
}

$infCandidates = @(Get-ChildItem -LiteralPath $cabRelease -Filter "*.inf" -File -ErrorAction SilentlyContinue)
if ($infCandidates.Count -eq 0) {
    Fail "未找到 $cabRelease 中的 INF。请使用 VS2008 的 positron_cab 项目执行 Release Build，或不要使用 -SkipCabBuild。"
}
$inf = $infCandidates | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1

$stagedInf = Join-Path $output "positron-nightly-cab-wm6-armv4i.inf"
Copy-Item -LiteralPath $inf.FullName -Destination $stagedInf -Force
Assert-InfFile $stagedInf $version $buildDate | Out-Null

$generatedCab = $finalCab
$cabCandidates = @(Get-ChildItem -LiteralPath $cabRelease -Filter "*.cab" -File -ErrorAction SilentlyContinue | Where-Object { $_.Name -ieq $finalName })
if ($cabCandidates.Count -ne 1) {
    Fail "VS2008 positron_cab 项目未生成唯一的 $finalName；详见 $cabBuildLog"
}
$sourceCab = $cabCandidates[0]
Copy-Item -LiteralPath $sourceCab.FullName -Destination $generatedCab -Force
if (-not (Test-Path -LiteralPath $generatedCab -PathType Leaf)) {
    Fail "无法复制 VS2008 生成的 CAB：$($sourceCab.FullName)"
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
    foreach ($forbidden in @("test_host", "positron_db\.dll", "positron_media\.dll", "fixtures", "\.pdb", "\.lib")) {
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
$hash = Get-Sha256Hex $generatedCab
[IO.File]::WriteAllText($finalSums, ($hash + "  " + $finalName + [Environment]::NewLine), (New-Object Text.UTF8Encoding($false)))
if (Test-Path -LiteralPath $legacyReadme) {
    Remove-Item -LiteralPath $legacyReadme -Force
}
$releaseNotes = @(
    "Windows Mobile 6 Professional / ARMV4I 的标准 Smart Device CAB。",
    "",
    "- Asset: $finalName",
    "- Channel/tag: $tag",
    "- Commit: $commit",
    "- Version: $version",
    "- BuildDate: $buildDate",
    "- Worktree at packaging time: $state",
    "- SHA256: $hash",
    "",
    "安装位置：\Program Files\Positron；公共 DLL 位于 \Windows；字体位于 \Windows\fonts；快捷方式位于 \Windows\Start Menu\Programs。",
    "",
    "CAB 不包含 positron_media.dll、test_host.exe、fixtures、PDB、LIB 或源码。安装、升级和卸载仍需在干净的 WM6 Professional ARMV4I 设备上完成验收。"
) -join [Environment]::NewLine

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
    $releaseViewErrorAction = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $gh release view $tag @repoArgs *> $null
    $releaseViewExitCode = $LASTEXITCODE
    $ErrorActionPreference = $releaseViewErrorAction
    $releaseAssetNames = @(
        [IO.Path]::GetFileName($generatedCab),
        [IO.Path]::GetFileName($finalSums)
    )
    $releaseWorkDirectory = Split-Path -Parent $generatedCab
    if ($releaseViewExitCode -ne 0) {
        Push-Location -LiteralPath $releaseWorkDirectory
        try {
            & $gh release create $tag @releaseAssetNames @repoArgs --title "Positron nightly CAB" --notes $releaseNotes --prerelease
            $releaseExitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
    }
    else {
        Push-Location -LiteralPath $releaseWorkDirectory
        try {
            & $gh release upload $tag @releaseAssetNames @repoArgs --clobber
            $releaseExitCode = $LASTEXITCODE
        }
        finally {
            Pop-Location
        }
        if ($releaseExitCode -eq 0) {
            & $gh release edit $tag @repoArgs --title "Positron nightly CAB" --notes $releaseNotes --prerelease
            $releaseExitCode = $LASTEXITCODE
        }
    }
    if ($releaseExitCode -ne 0) { Fail "nightly-cab release 上传失败" }
    & $gh release delete-asset $tag "NIGHTLY-CAB-README.md" @repoArgs --yes *> $null
}

Write-Host "CAB 完成：$generatedCab"
Write-Host "SHA256：$finalSums"
