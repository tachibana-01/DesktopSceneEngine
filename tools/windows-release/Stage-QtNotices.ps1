param(
    [Parameter(Mandatory)][string]$QtDirectory,
    [Parameter(Mandatory)][string]$SourceRoot,
    [Parameter(Mandatory)][string]$LicenseDirectory
)

$ErrorActionPreference = 'Stop'
$version = '6.10.1'
$qtbase = Join-Path $SourceRoot "qtbase-everywhere-src-$version"
foreach ($type in @('LGPL-3.0-only', 'GPL-3.0-only')) {
    Copy-Item -LiteralPath (Join-Path $qtbase "LICENSES/$type.txt") -Destination (Join-Path $LicenseDirectory "Qt-$type.txt")
}
Copy-Item -LiteralPath (Join-Path $qtbase 'src/3rdparty/freetype/docs/FTL.TXT') -Destination (Join-Path $LicenseDirectory 'Qt-FreeType-FTL.txt')
$bsd = Get-Content -LiteralPath (Join-Path $qtbase 'LICENSES/BSD-3-Clause.txt') -Raw
$entryPointSource = Get-Content -LiteralPath (Join-Path $qtbase 'src/entrypoint/qtentrypoint_win.cpp') -TotalCount 2
if ($entryPointSource[1] -notmatch 'BSD-3-Clause' -or $entryPointSource[0] -notmatch '^// (Copyright .+)$') {
    throw 'Unexpected Qt Windows entry point licensing'
}
$bsd.Replace('Copyright (c) <year> <owner>.', $Matches[1]) |
    Set-Content -LiteralPath (Join-Path $LicenseDirectory 'Qt-EntryPoint-BSD-3-Clause.txt') -Encoding utf8
$bsd.Replace('Copyright (c) <year> <owner>.', 'Copyright (C) The Qt Company Ltd. and other contributors.') |
    Set-Content -LiteralPath (Join-Path $LicenseDirectory 'Qt-Translations-BSD-3-Clause.txt') -Encoding utf8

$runtimeTargets = [ordered]@{
    qtbase = @('Core', 'Gui', 'Network', 'Widgets', 'QWindowsIntegrationPlugin', 'QICOPlugin', 'QJpegPlugin',
        'QGifPlugin', 'QTuioTouchPlugin', 'QModernWindowsStylePlugin', 'QNLMNIPlugin', 'QSchannelBackendPlugin', 'QTlsBackendCertOnlyPlugin')
    qtsvg = @('Svg', 'QSvgIconPlugin', 'QSvgPlugin')
}
$attributionDir = Join-Path $LicenseDirectory 'Qt-embedded'
[void][IO.Directory]::CreateDirectory($attributionDir)
$components = foreach ($module in $runtimeTargets.Keys) {
    $spdx = Get-Content -LiteralPath (Join-Path $QtDirectory "sbom/$module-$version.spdx.json") -Raw | ConvertFrom-Json -AsHashtable
    $packages = @{}
    foreach ($package in $spdx.packages) { $packages[$package.SPDXID] = $package }
    $direct = @($spdx.packages | Where-Object { $_.name -in $runtimeTargets[$module] })
    if ($direct.Count -ne $runtimeTargets[$module].Count -or
        @($direct | Where-Object { $_.licenseConcluded -notmatch 'LGPL-3\.0-only' }).Count) {
        throw "Unexpected licensing of selected Qt $module targets"
    }
    $queue = [Collections.Generic.Queue[string]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new()
    $written = [Collections.Generic.HashSet[string]]::new()
    foreach ($target in $direct) { $queue.Enqueue($target.SPDXID) }
    while ($queue.Count) {
        $id = $queue.Dequeue()
        if (!$seen.Add($id)) { continue }
        $package = $packages[$id]
        $deps = $spdx.relationships | Where-Object { $_.spdxElementId -eq $id -and $_.relationshipType -eq 'DEPENDS_ON' }
        foreach ($dep in $deps) {
            if ($packages.ContainsKey($dep.relatedSpdxElement)) { $queue.Enqueue($dep.relatedSpdxElement) }
        }
        if ($package['comment'] -notmatch "(?m)^\s*/src_dir/$module/(\S+qt_attribution\.json)\s*$") { continue }
        $attributionRelative = $Matches[1]
        if (!$attributionRelative.StartsWith('src/')) { continue } # CMake-only build inputs
        $attribution = Join-Path $SourceRoot "$module-everywhere-src-$version/$attributionRelative"
        if ($package.comment -notmatch '(?m)^\s*Id: ([\w-]+)\s*$') { throw "Attribution ID missing for $id" }
        $attributionId = $Matches[1]
        if (!$written.Add("$attributionRelative/$attributionId")) { continue }
        $entry = @(Get-Content -LiteralPath $attribution -Raw | ConvertFrom-Json -AsHashtable) | Where-Object { $_.Id -eq $attributionId }
        if (!$entry) { throw "Attribution input missing for $id" }
        $filename = "$module-$($package.name).txt"
        $targetPath = Join-Path $attributionDir $filename
        $copyright = if ($entry['CopyrightFile']) {
            Get-Content -LiteralPath (Join-Path (Split-Path -Parent $attribution) $entry['CopyrightFile']) -Raw
        } else { $entry['Copyright'] }
        $licenseFiles = @($entry['LicenseFile']) + @($entry['LicenseFiles']) | Where-Object { $_ }
        $body = @($entry.Name, "Version: $($package.versionInfo)", "License: $($entry['License'])", '', $copyright)
        foreach ($licenseFile in $licenseFiles) {
            $body += '', (Get-Content -LiteralPath (Join-Path (Split-Path -Parent $attribution) $licenseFile) -Raw)
        }
        $body | Set-Content -LiteralPath $targetPath -Encoding utf8
        [pscustomobject]@{
            QtModule = $module
            Component = $entry.Name
            Version = $package.versionInfo
            License = $package.licenseConcluded
            CopyrightAndLicense = "Qt-embedded/$filename"
        }
    }
}
$components | Sort-Object QtModule, Component | Export-Csv -LiteralPath (Join-Path $LicenseDirectory 'Qt-embedded-components.csv') -NoTypeInformation -Encoding utf8
