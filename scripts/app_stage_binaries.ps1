function Get-PositronAppStageBinaries {
    param([Parameter(Mandatory=$true)][string]$StageRoot)

    $expected = @(
        'positron.exe',
        'positron_tls.dll', 'positron_json.dll', 'positron_archive.dll',
        'positron_runtime.dll', 'positron_media.dll', 'positron_http.dll',
        'positron_core.dll', 'positron_image.dll', 'positron_script.dll',
        'positron_browser.dll', 'positron_db.dll'
    )
    $files = @(Get-ChildItem -LiteralPath $StageRoot -File -ErrorAction Stop |
        Where-Object { $_.Name -eq 'positron.exe' -or $_.Extension -eq '.dll' })
    $missing = @($expected | Where-Object { $_ -notin $files.Name })
    $unexpected = @($files.Name | Where-Object { $_ -notin $expected })
    if ($missing.Count -or $unexpected.Count) {
        throw ("Invalid full application stage. Missing: {0}. Unexpected: {1}." -f
            ($missing -join ', '), ($unexpected -join ', '))
    }
    $files
}
