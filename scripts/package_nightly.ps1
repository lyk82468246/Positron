[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release",
    [string] $Repository = "",
    [string] $Tag = "nightly",
    [string] $OutputDirectory = "",
    [switch] $SkipUpload
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version 2.0

function Invoke-CheckedText([string] $FilePath, [string[]] $Arguments)
{
    $output = & $FilePath @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        $details = ($output | ForEach-Object { $_.ToString() }) -join "`n"
        throw ("{0} failed with exit code {1}.`n{2}" -f $FilePath,
                $exitCode, $details.Trim())
    }
    return (($output | ForEach-Object { $_.ToString() }) -join "`n").Trim()
}

function Get-Sha256([string] $Path)
{
    $algorithm = New-Object System.Security.Cryptography.SHA256Managed
    $stream = [IO.File]::OpenRead($Path)
    try {
        $bytes = $algorithm.ComputeHash($stream)
        return ([BitConverter]::ToString($bytes).Replace("-", "")).ToLowerInvariant()
    } finally {
        $stream.Dispose()
        $algorithm.Dispose()
    }
}

function Add-TestRange([System.Collections.Generic.List[string]] $Ranges,
        [int] $Start, [int] $End)
{
    if ($Start -le $End) {
        [void] $Ranges.Add(("{0}-{1}" -f $Start, $End))
    }
}

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
if ([string]::IsNullOrEmpty($OutputDirectory)) {
    $outputRoot = Join-Path $repoRoot "tmp\nightly"
} elseif ([IO.Path]::IsPathRooted($OutputDirectory)) {
    $outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
} else {
    $outputRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputDirectory))
}

if ($Tag -notmatch "^[A-Za-z][A-Za-z0-9._-]*$") {
    throw "Tag must start with a letter and contain only letters, digits, dot, underscore or dash."
}

$notesSource = Join-Path $repoRoot "docs\NIGHTLY_RELEASE.md"
$mainSource = Join-Path $repoRoot "test_host\main.c"
if (!(Test-Path -LiteralPath $notesSource -PathType Leaf)) {
    throw "Release notes source is missing: $notesSource"
}
if (!(Test-Path -LiteralPath $mainSource -PathType Leaf)) {
    throw "test_host/main.c is missing; cannot derive the test range."
}

$mainText = [IO.File]::ReadAllText($mainSource, [Text.Encoding]::UTF8)
if ($mainText -notmatch "#define\s+TEST_MAX_NUMBER\s+(\d+)") {
    throw "Could not find TEST_MAX_NUMBER in $mainSource."
}
$testMax = [int] $Matches[1]
$testRanges = New-Object "System.Collections.Generic.List[string]"
Add-TestRange $testRanges 1 22
Add-TestRange $testRanges 24 77
Add-TestRange $testRanges 80 998
Add-TestRange $testRanges 1000 $testMax
$allTests = (($testRanges.ToArray() + @("7b", "999")) -join ",")

$artifactSpecs = @(
    @{ Relative = "positron_tls\bin\$Configuration\positron_tls.dll"; Archive = "positron_tls.dll" },
    @{ Relative = "positron_json\bin\$Configuration\positron_json.dll"; Archive = "positron_json.dll" },
    @{ Relative = "positron_http\bin\$Configuration\positron_http.dll"; Archive = "positron_http.dll" },
    @{ Relative = "positron_core\bin\$Configuration\positron_core.dll"; Archive = "positron_core.dll" },
    @{ Relative = "positron_image\bin\$Configuration\positron_image.dll"; Archive = "positron_image.dll" },
    @{ Relative = "positron_script\bin\$Configuration\positron_script.dll"; Archive = "positron_script.dll" },
    @{ Relative = "positron_browser\bin\$Configuration\positron_browser.dll"; Archive = "positron_browser.dll" },
    @{ Relative = "test_host\bin\$Configuration\test_host.exe"; Archive = "test_host.exe" }
)

$assetSpecs = @(
    @{ Relative = "assets\fonts\PositronSymbolsBasic.ttf"; Archive = "fonts\PositronSymbolsBasic.ttf" },
    @{ Relative = "assets\fonts\PositronSymbols.ttf"; Archive = "fonts\PositronSymbols.ttf" },
    @{ Relative = "assets\fonts\PositronEmoji.ttf"; Archive = "fonts\PositronEmoji.ttf" },
    @{ Relative = "third_party\noto-symbols\OFL.txt"; Archive = "fonts\OFL-NotoSymbols.txt" },
    @{ Relative = "third_party\noto-symbols2\OFL.txt"; Archive = "fonts\OFL-NotoSymbols2.txt" },
    @{ Relative = "third_party\noto-emoji\OFL.txt"; Archive = "fonts\OFL-NotoEmoji.txt" },
    @{ Relative = "LICENSE"; Archive = "LICENSE" },
    @{ Relative = "THIRD_PARTY.md"; Archive = "THIRD_PARTY.md" }
)

foreach ($spec in ($artifactSpecs + $assetSpecs)) {
    $sourcePath = Join-Path $repoRoot $spec.Relative
    if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw ("Required package input is missing: {0}" -f $sourcePath)
    }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$workRoot = Join-Path $outputRoot ("package-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $workRoot -Force | Out-Null
$zipPath = Join-Path $outputRoot "positron-nightly.zip"
$iniPath = Join-Path $workRoot "test_host.ini"
$readmePath = Join-Path $workRoot "NIGHTLY-README.md"
$manifestPath = Join-Path $workRoot "SHA256SUMS.txt"
$zipInputPath = Join-Path $workRoot "zip-input.tsv"

try {
    Push-Location $repoRoot

    $commit = Invoke-CheckedText "git" @("rev-parse", "HEAD")
    $shortCommit = Invoke-CheckedText "git" @("rev-parse", "--short", "HEAD")
    $generatedUtc = [DateTime]::UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ")

    $iniLines = @(
        "# Positron nightly: all currently available tests in automated mode.",
        "# TEST 23, 78 and 79 are withdrawn and intentionally excluded.",
        "# Edit tests= for a partial run; change auto=0 for manual prompts.",
        "auto=1",
        "javascript=0",
        ("tests=" + $allTests)
    )
    [IO.File]::WriteAllText($iniPath, (($iniLines -join "`r`n") + "`r`n"),
            (New-Object Text.UTF8Encoding($false)))

    $notes = [IO.File]::ReadAllText($notesSource, [Text.Encoding]::UTF8)
    $notes += @(
        "",
        "---",
        "",
        "Package metadata",
        ("- Configuration: " + $Configuration),
        ("- Source commit: " + $commit),
        ("- Generated (UTC): " + $generatedUtc),
        "- Archive: positron-nightly.zip (ZIP store / no compression)",
        ("- Available test ceiling: TEST_MAX_NUMBER " + $testMax)
    ) -join "`r`n"
    [IO.File]::WriteAllText($readmePath, $notes,
            (New-Object Text.UTF8Encoding($false)))

    $packageFiles = @()
    foreach ($spec in ($artifactSpecs + $assetSpecs)) {
        $packageFiles += [pscustomobject]@{
            Source = Join-Path $repoRoot $spec.Relative
            Archive = $spec.Archive
        }
    }
    $packageFiles += [pscustomobject]@{ Source = $iniPath; Archive = "test_host.ini" }
    $packageFiles += [pscustomobject]@{ Source = $readmePath; Archive = "NIGHTLY-README.md" }

    $manifestLines = @(
        "# SHA-256 checksums for the stored nightly package entries",
        ("# configuration=" + $Configuration),
        ("# commit=" + $commit)
    )
    foreach ($file in $packageFiles) {
        $manifestLines += ((Get-Sha256 $file.Source) + "  " + $file.Archive)
    }
    [IO.File]::WriteAllText($manifestPath, (($manifestLines -join "`r`n") + "`r`n"),
            (New-Object Text.UTF8Encoding($false)))

    $zipInputLines = @()
    foreach ($file in $packageFiles) {
        $zipInputLines += ($file.Source + "`t" + $file.Archive)
    }
    $zipInputLines += ($manifestPath + "`tSHA256SUMS.txt")
    [IO.File]::WriteAllText($zipInputPath,
            (($zipInputLines -join "`r`n") + "`r`n"),
            (New-Object Text.UTF8Encoding($false)))

    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($null -eq $python) {
        throw "Python 3 was not found; it is required to write a guaranteed ZIP_STORED archive."
    }
    $zipOutput = & $python.Source (Join-Path $PSScriptRoot "write_stored_zip.py") `
            --manifest $zipInputPath --output $zipPath 2>&1
    if ($LASTEXITCODE -ne 0) {
        $details = ($zipOutput | ForEach-Object { $_.ToString() }) -join "`n"
        throw ("write_stored_zip.py failed.`n{0}" -f $details.Trim())
    }
    $entryCount = (($zipOutput | Select-String -Pattern "stored_entries=(\d+)" |
            Select-Object -First 1).Matches.Groups[1].Value)
    if ([string]::IsNullOrEmpty($entryCount)) {
        throw "write_stored_zip.py did not report the stored entry count."
    }

    if ($SkipUpload) {
        Write-Host ("Nightly package created without upload: " + $zipPath)
    } else {
        if ($null -eq (Get-Command gh -ErrorAction SilentlyContinue)) {
            throw "GitHub CLI (gh) was not found. Install it and authenticate before uploading."
        }
        # Windows PowerShell promotes native stderr to NativeCommandError when
        # ErrorActionPreference is Stop. Capture it as text so the caller gets
        # the actionable login guidance below instead of a truncated gh error.
        $authPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = "Continue"
            $authOutput = & gh auth status 2>&1
            $authExitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $authPreference
        }
        if ($authExitCode -ne 0) {
            $details = ($authOutput | ForEach-Object { $_.ToString() }) -join "`n"
            throw ("GitHub CLI authentication is not ready. Run 'gh auth login -h github.com'.`n{0}" -f $details.Trim())
        }
        $repoArgs = @()
        if (![string]::IsNullOrEmpty($Repository)) {
            $repoArgs = @("--repo", $Repository)
        }
        $viewOutput = & gh release view $Tag @repoArgs --json tagName 2>$null
        $releaseExists = ($LASTEXITCODE -eq 0)
        if ($releaseExists) {
            & gh release edit $Tag @repoArgs --title "Positron nightly" `
                --notes-file $readmePath --prerelease
            if ($LASTEXITCODE -ne 0) {
                throw "Could not update the existing nightly pre-release."
            }
        } else {
            & gh release create $Tag @repoArgs --title "Positron nightly" `
                --notes-file $readmePath --prerelease --target $commit
            if ($LASTEXITCODE -ne 0) {
                throw "Could not create the nightly pre-release."
            }
        }
        & gh release upload $Tag $zipPath @repoArgs --clobber
        if ($LASTEXITCODE -ne 0) {
            throw "Could not upload or replace positron-nightly.zip."
        }
        Write-Host ("Uploaded nightly pre-release '{0}' and replaced positron-nightly.zip." -f $Tag)
    }
    Write-Host ("Stored ZIP entries: " + $entryCount)
    Write-Host ("Source commit: " + $shortCommit)
} finally {
    Pop-Location -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $workRoot) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
