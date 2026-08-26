[CmdletBinding()]
param(
    [ValidateSet("Auto", "Debug", "Release")]
    [string] $Configuration = "Auto",
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

function Invoke-GhCaptured([string[]] $Arguments)
{
    # Windows PowerShell promotes native stderr to NativeCommandError when
    # ErrorActionPreference is Stop. Keep gh's exit code and text together so
    # expected probes (such as a missing first release) remain controllable.
    $preference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $output = & gh @Arguments 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $preference
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
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
        if ($Start -eq $End) {
            [void] $Ranges.Add(("{0}" -f $Start))
        } else {
            [void] $Ranges.Add(("{0}-{1}" -f $Start, $End))
        }
    }
}

function Convert-TestNumbersToSelection([int[]] $Numbers)
{
    $ranges = New-Object "System.Collections.Generic.List[string]"
    if ($null -eq $Numbers -or $Numbers.Length -eq 0) {
        return @()
    }
    $start = $Numbers[0]
    $end = $Numbers[0]
    for ($index = 1; $index -lt $Numbers.Length; $index++) {
        if ($Numbers[$index] -eq ($end + 1)) {
            $end = $Numbers[$index]
        } else {
            Add-TestRange $ranges $start $end
            $start = $Numbers[$index]
            $end = $Numbers[$index]
        }
    }
    Add-TestRange $ranges $start $end
    return $ranges.ToArray()
}

function Get-ArtifactSpecs([string] $Config)
{
    return @(
        @{ Relative = ("positron_tls\bin\{0}\positron_tls.dll" -f $Config); Archive = "positron_tls.dll" },
        @{ Relative = ("positron_json\bin\{0}\positron_json.dll" -f $Config); Archive = "positron_json.dll" },
        @{ Relative = ("positron_http\bin\{0}\positron_http.dll" -f $Config); Archive = "positron_http.dll" },
        @{ Relative = ("positron_core\bin\{0}\positron_core.dll" -f $Config); Archive = "positron_core.dll" },
        @{ Relative = ("positron_image\bin\{0}\positron_image.dll" -f $Config); Archive = "positron_image.dll" },
        @{ Relative = ("positron_script\bin\{0}\positron_script.dll" -f $Config); Archive = "positron_script.dll" },
        @{ Relative = ("positron_browser\bin\{0}\positron_browser.dll" -f $Config); Archive = "positron_browser.dll" },
        @{ Relative = ("test_host\bin\{0}\test_host.exe" -f $Config); Archive = "test_host.exe" }
    )
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

$dispatchMatch = [regex]::Match($mainText,
        "(?s)static\s+int\s+run_configured_tests\s*\(.*?default:\s*ok\s*=\s*FALSE;\s*break;")
if (!$dispatchMatch.Success) {
    throw "Could not find the test dispatch terminator in $mainSource."
}
$dispatchText = $dispatchMatch.Value
$testNumbers = New-Object "System.Collections.Generic.List[int]"
foreach ($match in [regex]::Matches($dispatchText, "(?m)^\s*case\s+(\d+)\s*:")) {
    $number = [int] $match.Groups[1].Value
    if ($number -gt $testMax) {
        throw ("Test dispatch case {0} exceeds TEST_MAX_NUMBER {1}." -f
                $number, $testMax)
    }
    if (!$testNumbers.Contains($number)) {
        [void] $testNumbers.Add($number)
    }
}
# TEST7b is a deliberate non-numeric branch beside case 7. The completion
# beep number is read from the source instead of being copied into a range.
if ($dispatchText -notmatch "number\s*==\s*7") {
    throw "Could not find the TEST7/TEST7b dispatch branch in $mainSource."
}
if (!$testNumbers.Contains(7)) {
    [void] $testNumbers.Add(7)
}

# A manual-only fixture is a real test, but it cannot be placed in the
# default auto=1 package: its implementation deliberately returns FALSE when
# automation is enabled. Discover those IDs from the source message instead
# of maintaining another hand-written test catalogue.
$manualOnlyNumbers = New-Object "System.Collections.Generic.List[int]"
foreach ($match in [regex]::Matches($mainText,
        '(?m)show_error\s*\(\s*L"TEST\s+(\d+)\s+SKIPPED"\s*,\s*"This test is manual-only')) {
    $manualNumber = [int] $match.Groups[1].Value
    if ($manualNumber -gt $testMax) {
        throw ("Manual-only TEST{0} exceeds TEST_MAX_NUMBER {1}." -f
                $manualNumber, $testMax)
    }
    if (!$manualOnlyNumbers.Contains($manualNumber)) {
        [void] $manualOnlyNumbers.Add($manualNumber)
    }
}
foreach ($manualNumber in $manualOnlyNumbers) {
    [void] $testNumbers.Remove($manualNumber)
}
if ($mainText -notmatch "#define\s+TEST_COMPLETION_BEEP_NUMBER\s+(\d+)") {
    throw "Could not find TEST_COMPLETION_BEEP_NUMBER in $mainSource."
}
$completionBeepNumber = [int] $Matches[1]
if ($completionBeepNumber -gt $testMax) {
    throw ("Completion beep TEST{0} exceeds TEST_MAX_NUMBER {1}." -f
            $completionBeepNumber, $testMax)
}
$testNumbers.Sort()
$allTests = ((Convert-TestNumbersToSelection $testNumbers.ToArray()) +
        @("7b", [string] $completionBeepNumber)) -join ","
$manualOnlyNumbers.Sort()
$manualOnlyTests = (Convert-TestNumbersToSelection $manualOnlyNumbers.ToArray()) -join ","

$artifactCandidates = @("Debug", "Release")
$artifactSpecs = $null
$packageConfiguration = $Configuration
if ($Configuration -eq "Auto") {
    $completeConfigurations = @()
    foreach ($candidate in $artifactCandidates) {
        $candidateSpecs = Get-ArtifactSpecs $candidate
        $candidateFiles = @()
        $candidateComplete = $true
        foreach ($spec in $candidateSpecs) {
            $sourcePath = Join-Path $repoRoot $spec.Relative
            if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
                $candidateComplete = $false
                break
            }
            $candidateFiles += Get-Item -LiteralPath $sourcePath
        }
        if ($candidateComplete) {
            $oldest = $candidateFiles[0].LastWriteTimeUtc
            foreach ($candidateFile in $candidateFiles) {
                if ($candidateFile.LastWriteTimeUtc -lt $oldest) {
                    $oldest = $candidateFile.LastWriteTimeUtc
                }
            }
            $completeConfigurations += [pscustomobject]@{
                Configuration = $candidate
                OldestArtifact = $oldest
            }
        }
    }
    if ($completeConfigurations.Count -eq 0) {
        throw "Neither Debug nor Release has a complete set of package binaries."
    }
    $selected = $completeConfigurations |
        Sort-Object -Property OldestArtifact -Descending |
        Select-Object -First 1
    $packageConfiguration = $selected.Configuration
}
$artifactSpecs = Get-ArtifactSpecs $packageConfiguration

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
        "# Positron nightly: all tests currently dispatched by test_host/main.c.",
        "# The list is generated from run_configured_tests; it is not copied from the smoke INI.",
        "# Edit tests= for a partial run; change auto=0 for manual prompts.",
        "auto=1",
        "javascript=0",
        ("tests=" + $allTests)
    )
    if ($manualOnlyNumbers.Count -gt 0) {
        $iniLines = @(
            "# Positron nightly: all automation-safe tests currently dispatched by test_host/main.c.",
            "# The list is generated from run_configured_tests; it is not copied from the smoke INI.",
            ("# Manual-only tests omitted from auto=1: " + $manualOnlyTests + "."),
            "# For full manual coverage, append those IDs to tests=, set auto=0, and set javascript=1 when required.",
            "# Edit tests= for a partial run; change auto=0 for manual prompts.",
            "auto=1",
            "javascript=0",
            ("tests=" + $allTests)
        )
    }
    [IO.File]::WriteAllText($iniPath, (($iniLines -join "`r`n") + "`r`n"),
            (New-Object Text.UTF8Encoding($false)))

    $notes = [IO.File]::ReadAllText($notesSource, [Text.Encoding]::UTF8)
    $notes += @(
        "",
        "---",
        "",
        "Package metadata",
        ("- Configuration: " + $packageConfiguration),
        ("- Source commit: " + $commit),
        ("- Generated (UTC): " + $generatedUtc),
        "- Archive: positron-nightly.zip (ZIP store / no compression)",
        ("- Available test ceiling: TEST_MAX_NUMBER " + $testMax),
        ("- Generated test selection: " + $allTests)
    ) -join "`r`n"
    if ($manualOnlyNumbers.Count -gt 0) {
        $notes += @(
            "",
            ("- Manual-only tests omitted from auto=1: " + $manualOnlyTests)
        ) -join "`r`n"
    }
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
        ("# configuration=" + $packageConfiguration),
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
        $authResult = Invoke-GhCaptured @("auth", "status")
        if ($authResult.ExitCode -ne 0) {
            $details = ($authResult.Output | ForEach-Object { $_.ToString() }) -join "`n"
            throw ("GitHub CLI authentication is not ready. Run 'gh auth login -h github.com'.`n{0}" -f $details.Trim())
        }
        $repoArgs = @()
        if (![string]::IsNullOrEmpty($Repository)) {
            $repoArgs = @("--repo", $Repository)
        }
        $viewArgs = @("release", "view", $Tag) + $repoArgs +
                @("--json", "tagName")
        $viewResult = Invoke-GhCaptured $viewArgs
        $viewDetails = ($viewResult.Output |
                ForEach-Object { $_.ToString() }) -join "`n"
        if ($viewResult.ExitCode -eq 0) {
            $editArgs = @("release", "edit", $Tag) + $repoArgs +
                    @("--title", "Positron nightly", "--notes-file",
                    $readmePath, "--prerelease")
            $editResult = Invoke-GhCaptured $editArgs
            if ($editResult.ExitCode -ne 0) {
                $details = ($editResult.Output |
                        ForEach-Object { $_.ToString() }) -join "`n"
                throw ("Could not update the existing nightly pre-release.`n{0}" -f
                        $details.Trim())
            }
        } elseif ($viewDetails -match "(?i)release\s+not\s+found|not\s+found") {
            $createArgs = @("release", "create", $Tag) + $repoArgs +
                    @("--title", "Positron nightly", "--notes-file",
                    $readmePath, "--prerelease", "--target", $commit)
            $createResult = Invoke-GhCaptured $createArgs
            if ($createResult.ExitCode -ne 0) {
                $details = ($createResult.Output |
                        ForEach-Object { $_.ToString() }) -join "`n"
                throw ("Could not create the nightly pre-release.`n{0}" -f
                        $details.Trim())
            }
        } else {
            throw ("Could not inspect the nightly pre-release.`n{0}" -f
                    $viewDetails.Trim())
        }
        # gh treats '#' inside an asset argument as the start of a display
        # label. The repository path itself contains "C#", so use a path
        # relative to the repository whenever the ZIP is under that root.
        $uploadAssetPath = $zipPath
        $repoPrefix = $repoRoot.TrimEnd([IO.Path]::DirectorySeparatorChar,
                [IO.Path]::AltDirectorySeparatorChar) +
                [IO.Path]::DirectorySeparatorChar
        if ($zipPath.StartsWith($repoPrefix,
                [StringComparison]::OrdinalIgnoreCase)) {
            $uploadAssetPath = $zipPath.Substring($repoPrefix.Length)
        }
        if ($uploadAssetPath.IndexOf("#", [StringComparison]::Ordinal) -ge 0) {
            throw "Upload asset path contains '#', which GitHub CLI reserves for asset labels. Choose an output directory without '#'."
        }
        $uploadArgs = @("release", "upload", $Tag, $uploadAssetPath) + $repoArgs +
                @("--clobber")
        $uploadResult = Invoke-GhCaptured $uploadArgs
        if ($uploadResult.ExitCode -ne 0) {
            $details = ($uploadResult.Output |
                    ForEach-Object { $_.ToString() }) -join "`n"
            throw ("Could not upload or replace positron-nightly.zip.`n{0}" -f
                    $details.Trim())
        }
        Write-Host ("Uploaded nightly pre-release '{0}' and replaced positron-nightly.zip." -f $Tag)
    }
    Write-Host ("Stored ZIP entries: " + $entryCount)
    Write-Host ("Package configuration: " + $packageConfiguration)
    Write-Host ("Source commit: " + $shortCommit)
} finally {
    Pop-Location -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $workRoot) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
