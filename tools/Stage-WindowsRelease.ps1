#Requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$RuntimeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$QtDirectory,
    [Parameter(Mandatory)][string]$QtSourceDirectory,
    [Parameter(Mandatory)][string]$VcpkgDirectory,
    [Parameter(Mandatory)][string]$VcpkgTripletDirectory,
    [Parameter(Mandatory)][string]$Dumpbin
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repo = Split-Path -Parent $PSScriptRoot
$frontend = Join-Path $repo 'DesktopSceneEngineIII/Frontend'
$output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw "Output must be a new directory: $output" }

function Assert-Hash($path, $expected, $algorithm = 'SHA256') {
    if ((Get-FileHash -LiteralPath $path -Algorithm $algorithm).Hash -ne $expected) {
        throw "Unexpected $algorithm for $path"
    }
}

function Assert-SameFile($left, $right) {
    Assert-Hash $left (Get-FileHash -LiteralPath $right).Hash
}

function Copy-File($source, $destination) {
    if (!(Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing input: $source" }
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $destination))
    Copy-Item -LiteralPath $source -Destination $destination
}

function Write-File($path, $content) {
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $path))
    Set-Content -LiteralPath $path -Value $content -Encoding utf8
}

$qtVersion = '6.10.1'
$qtSourceHashes = [ordered]@{
    qtbase = '5a6226f7e23db51fdc3223121eba53f3f5447cf0cc4d6cb82a3a2df7a65d265d'
    qtsvg = 'c02f355a58f3bbcf404a628bf488b6aeb2d84a94c269afdb86f6e529343ab01f'
}
$qtLibraries = @('Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Network.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll')
$qtPlugins = @(
    'platforms/qwindows.dll', 'styles/qmodernwindowsstyle.dll',
    'imageformats/qgif.dll', 'imageformats/qico.dll', 'imageformats/qjpeg.dll', 'imageformats/qsvg.dll',
    'iconengines/qsvgicon.dll', 'generic/qtuiotouchplugin.dll', 'networkinformation/qnetworklistmanager.dll',
    'tls/qcertonlybackend.dll', 'tls/qschannelbackend.dll'
)
$ffmpegLibraries = @('avcodec-62.dll', 'avfilter-11.dll', 'avformat-62.dll', 'avutil-60.dll', 'swresample-6.dll', 'swscale-9.dll')
$player = 'DesktopSceneEngine_QtPlayer.exe'
$qtTranslation = 'qtbase_zh_CN.qm'
$nativeFiles = @($player) + $qtLibraries + $qtPlugins + $ffmpegLibraries

$qtInputs = @{}
foreach ($name in $qtSourceHashes.Keys) {
    $archive = Join-Path $QtSourceDirectory "$name-everywhere-src-$qtVersion.tar.xz"
    Assert-Hash $archive $qtSourceHashes[$name]
    $qtInputs[$name] = $archive
}
foreach ($relative in $qtLibraries + $qtPlugins) {
    $sourceDir = if ($relative -in $qtLibraries) { 'bin' } else { 'plugins' }
    $source = Join-Path $QtDirectory "$sourceDir/$relative"
    Assert-SameFile (Join-Path $RuntimeDirectory $relative) $source
    $signature = Get-AuthenticodeSignature -LiteralPath $source
    if ((Get-Item -LiteralPath $source).VersionInfo.FileVersion -ne "$qtVersion.0" -or
        $signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'CN=The Qt Company Oy,') {
        throw "Unexpected Qt binary provenance: $relative"
    }
}
Assert-SameFile (Join-Path $RuntimeDirectory "i18n/$qtTranslation") (Join-Path $QtDirectory "translations/$qtTranslation")
Assert-Hash (Join-Path $RuntimeDirectory "i18n/$qtTranslation") '098a5e471925d5de1825321383ce59178a8286b7' 'SHA1'

$webView = (Get-Content -LiteralPath (Join-Path $repo 'DesktopSceneEngineIII/dependency-lock.json') -Raw | ConvertFrom-Json).webView2
if ($webView.loaderStrategy -ne 'static' -or $webView.architecture -ne 'x64') { throw 'Expected static x64 WebView2 Loader' }
$deps = Join-Path $repo 'DesktopSceneEngineIII/.deps'
$sdk = Join-Path $deps "$($webView.packageId).$($webView.version)"
$nupkg = Join-Path $deps "downloads/$($webView.packageId).$($webView.version).nupkg"
Assert-Hash $nupkg $webView.sha256
Assert-Hash (Join-Path $sdk 'build/native/x64/WebView2LoaderStatic.lib') $webView.loaderSha256
$sdkArchive = [IO.Compression.ZipFile]::OpenRead($nupkg)
try {
    foreach ($notice in @('LICENSE.txt', 'NOTICE.txt')) {
        $entry = $sdkArchive.GetEntry($notice)
        if (!$entry) { throw "WebView2 SDK package missing $notice" }
        $stream = $entry.Open()
        try { Assert-Hash (Join-Path $sdk $notice) (Get-FileHash -InputStream $stream).Hash }
        finally { $stream.Dispose() }
    }
} finally { $sdkArchive.Dispose() }

$runtimeImports = foreach ($relative in $nativeFiles) {
    $binary = Join-Path $RuntimeDirectory $relative
    $imports = & $Dumpbin /nologo /dependents $binary
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect PE imports of $relative" }
    $depends = @($imports | ForEach-Object {
        if ($_ -match '^    ([\w-]+\.dll)\s*$') { $Matches[1] }
    } | Sort-Object -Unique)
    if (!$depends.Count) { throw "No PE imports found in $relative" }
    foreach ($dll in $depends) {
        $provider = if ($dll -in $nativeFiles) { 'adjacent packaged DLL' }
        elseif ($dll -in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
            'Microsoft Visual C++ v14 x64 Redistributable'
        } elseif ($dll -match '^(api|ext)-ms-win-' -or
            ($dll -notmatch '^(Qt|avcodec|avdevice|avfilter|avformat|avutil|swscale|swresample|WebView2Loader|lib|msvcp|vcruntime|ucrtbase[d]|dxcompiler|dxil)' -and
             (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32/$dll") -PathType Leaf))) { 'Windows 11 x64' }
        else { throw "Unresolved shipping PE import $relative -> $dll" }
        [pscustomobject]@{ Binary = $relative; Import = $dll; Provider = $provider }
    }
}

$frontendLock = (Get-Content -LiteralPath (Join-Path $frontend 'package-lock.json') -Raw | ConvertFrom-Json -AsHashtable).packages
$frontendDeps = [ordered]@{ react = 'LICENSE'; 'react-dom' = 'LICENSE'; scheduler = 'LICENSE'; vite = 'LICENSE.md'; rolldown = 'LICENSE' }
$frontendVersions = foreach ($name in $frontendDeps.Keys) {
    $package = Get-Content -LiteralPath (Join-Path $frontend "node_modules/$name/package.json") -Raw | ConvertFrom-Json
    if ($package.version -ne $frontendLock."node_modules/$name".version -or $package.license -ne 'MIT') {
        throw "Unmatched frontend lock or license: $name"
    }
    "$name $($package.version)"
}
$launcherRoot = Join-Path (Resolve-Path -LiteralPath $RuntimeDirectory).Path 'launcher'
$frontendFiles = @(Get-ChildItem -LiteralPath $launcherRoot -File -Recurse -Force)
$html = Get-Content -LiteralPath (Join-Path $launcherRoot 'index.html') -Raw
if ($frontendFiles.Count -ne 3 -or @($frontendFiles | Where-Object Extension -eq '.js').Count -ne 1 -or
    @($frontendFiles | Where-Object Extension -eq '.css').Count -ne 1) {
    throw 'Unexpected frontend output. Inspect its runtime code and assets before updating package scope.'
}
foreach ($file in $frontendFiles) {
    $relative = $file.FullName.Substring($launcherRoot.Length + 1).Replace('\', '/')
    if ($relative -notmatch '^(index\.html|assets/index-[\w-]+\.(js|css))$') { throw "Unexpected frontend asset: $relative" }
    if ($relative -ne 'index.html' -and !$html.Contains("./$relative")) { throw "Frontend entry does not reference $relative" }
    Assert-SameFile $file.FullName (Join-Path $frontend "dist/$relative")
}

$ffmpegShare = Join-Path $VcpkgTripletDirectory 'share/ffmpeg'
$ffmpegSpdx = Get-Content -LiteralPath (Join-Path $ffmpegShare 'vcpkg.spdx.json') -Raw | ConvertFrom-Json
$ffmpegVersion = ($ffmpegSpdx.packages | Where-Object name -eq 'ffmpeg').versionInfo
if ($ffmpegVersion -ne '8.1.2#3') { throw "Unreviewed FFmpeg port $ffmpegVersion" }
$ffmpegConfig = Join-Path $VcpkgDirectory 'buildtrees/ffmpeg/x64-windows-rel'
$configureLine = Get-Content -LiteralPath (Join-Path $ffmpegConfig 'config.h') | Where-Object { $_ -match '^#define FFMPEG_CONFIGURATION ' }
if ($configureLine -notmatch '^#define FFMPEG_CONFIGURATION "(.+)"$') { throw 'Missing FFmpeg Release configure line' }
$configure = $Matches[1]
if ($configure -match '--enable-(gpl|nonfree|version3)(\s|$)' -or $configure -notmatch '--disable-static --enable-shared') {
    throw 'Unexpected FFmpeg license/linkage configuration'
}
foreach ($relative in $ffmpegLibraries) {
    $binary = Join-Path $RuntimeDirectory $relative
    Assert-SameFile $binary (Join-Path $VcpkgTripletDirectory "bin/$relative")
    $binaryText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $binary).Path))
    if (!$binaryText.Contains($configure)) { throw "FFmpeg Release configuration does not match $relative" }
}
$upstreamFfmpeg = Join-Path $VcpkgDirectory 'downloads/ffmpeg-ffmpeg-n8.1.2.tar.gz'
$upstreamSource = $ffmpegSpdx.packages | Where-Object { $_.downloadLocation -eq 'git+https://github.com/ffmpeg/ffmpeg@n8.1.2' }
Assert-Hash $upstreamFfmpeg ($upstreamSource.checksums | Where-Object algorithm -eq 'SHA512').checksumValue 'SHA512'
$ffmpegPort = Join-Path $VcpkgDirectory 'ports/ffmpeg'
$abi = Get-Content -LiteralPath (Join-Path $ffmpegShare 'vcpkg_abi_info.txt') -Raw | ConvertFrom-StringData -Delimiter ' '
$portInputs = Get-ChildItem -LiteralPath $ffmpegPort -File
foreach ($file in $portInputs) { Assert-Hash $file.FullName $abi[$file.Name] }
$tripletHash = ($abi['triplet_abi'] -split '-')[0]
$triplet = Join-Path $VcpkgDirectory 'triplets/x64-windows.cmake'
Assert-Hash $triplet $tripletHash

$work = Join-Path (Split-Path -Parent $output) ('.release-materials-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($work)
try {
    [void](New-Item -ItemType Directory -Path $output)
    $thirdParty = Join-Path $output 'third_party'
    $licenses = Join-Path $thirdParty 'licenses'
    $sources = Join-Path $thirdParty 'sources'
    $buildInfo = Join-Path $thirdParty 'build-info'
    foreach ($relative in $nativeFiles + @('i18n/DesktopSceneEngine_zh_CN.qm', "i18n/$qtTranslation")) {
        Copy-File (Join-Path $RuntimeDirectory $relative) (Join-Path $output $relative)
    }
    foreach ($file in $frontendFiles) {
        Copy-File $file.FullName (Join-Path $output "launcher/$($file.FullName.Substring($launcherRoot.Length + 1))")
    }
    Copy-File (Join-Path $repo 'LICENSE') (Join-Path $output 'LICENSE')
    Copy-File (Join-Path $PSScriptRoot 'windows-release/THIRD_PARTY_NOTICES.md') (Join-Path $output 'THIRD_PARTY_NOTICES.md')
    Copy-File (Join-Path $sdk 'LICENSE.txt') (Join-Path $licenses 'WebView2-LICENSE.txt')
    Copy-File (Join-Path $sdk 'NOTICE.txt') (Join-Path $licenses 'WebView2-NOTICE.txt')
    foreach ($name in $frontendDeps.Keys) {
        Copy-File (Join-Path $frontend "node_modules/$name/$($frontendDeps[$name])") (Join-Path $licenses "$name-LICENSE.txt")
    }
    Copy-File (Join-Path $frontend 'node_modules/rolldown/THIRD-PARTY-LICENSE') (Join-Path $licenses 'rolldown-THIRD-PARTY-LICENSE.txt')
    Copy-File (Join-Path $ffmpegShare 'copyright') (Join-Path $licenses 'FFmpeg-LGPL-2.1.txt')
    $ffmpegLicensing = & tar.exe -xOf $upstreamFfmpeg 'FFmpeg-n8.1.2/LICENSE.md'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read FFmpeg upstream license overview' }
    Write-File (Join-Path $licenses 'FFmpeg-upstream-LICENSE.md') $ffmpegLicensing
    Copy-File (Join-Path $VcpkgTripletDirectory 'share/nlohmann-json/copyright') (Join-Path $licenses 'nlohmann-json-MIT.txt')
    foreach ($name in $qtInputs.Keys) {
        $archive = $qtInputs[$name]
        Copy-File $archive (Join-Path $sources (Split-Path -Leaf $archive))
        & tar.exe -xf $archive -C $work
        if ($LASTEXITCODE -ne 0) { throw "Qt source extraction failed: $name" }
    }
    & (Join-Path $PSScriptRoot 'windows-release/Stage-QtNotices.ps1') -QtDirectory $QtDirectory -SourceRoot $work -LicenseDirectory $licenses
    foreach ($name in @('qtbase', 'qtsvg', 'qttranslations')) {
        # Keep the vendor options, changing machine-specific absolute paths to symbolic placeholders.
        $options = Get-Content -LiteralPath (Join-Path $QtDirectory "config_$name.opt")
        $portableOptions = $options -replace '(?i)(?<=[:=])(?:[A-Z]:\\|/Users/)[^\r\n]+', '<LOCAL_BUILD_PATH>'
        Write-File (Join-Path $buildInfo "$name-$qtVersion.options.txt") $portableOptions
        $summary = Get-Content -LiteralPath (Join-Path $QtDirectory "config_$name.summary")
        if (($portableOptions + $summary) -match '(?i)(?<![\w:/])(?:[A-Z]:[/\\]|/Users/|/home/)') {
            throw "Qt build configuration contains an unhandled workstation path: $name"
        }
        Write-File (Join-Path $buildInfo "$name-$qtVersion.summary.txt") $summary
    }

    $ffmpegWork = Join-Path $work 'ffmpeg-source'
    Copy-File $upstreamFfmpeg (Join-Path $ffmpegWork (Split-Path -Leaf $upstreamFfmpeg))
    Copy-File (Join-Path $ffmpegShare 'copyright') (Join-Path $ffmpegWork 'COPYING.LGPLv2.1')
    Copy-File $triplet (Join-Path $ffmpegWork 'vcpkg/triplets/x64-windows.cmake')
    foreach ($file in $portInputs) { Copy-File $file.FullName (Join-Path $ffmpegWork "vcpkg/ports/ffmpeg/$($file.Name)") }
    $recipeHashes = @("$tripletHash  vcpkg/triplets/x64-windows.cmake") + @($portInputs | Sort-Object Name | ForEach-Object {
        "$($abi[$_.Name])  vcpkg/ports/ffmpeg/$($_.Name)"
    })
    Write-File (Join-Path $ffmpegWork 'VCPKG_SHA256SUMS.txt') $recipeHashes
    Copy-File (Join-Path $ffmpegConfig 'config_components.h') (Join-Path $buildInfo 'FFmpeg-config_components.h')
    $portableConfigure = $configure -replace "(--prefix=')[^']+", '$1<VCPKG_PACKAGE_ROOT>' `
        -replace "(--pkg-config=')[^']+", '$1<PKG_CONFIG_EXE>' `
        -replace "(--extra-ldflags='-libpath:)[^']+", '$1<VCPKG_TRIPLET_LIB>'
    if ($portableConfigure -match '(?i)[A-Z]:[/\\]') { throw 'FFmpeg build configuration contains an unhandled workstation path' }
    $ffmpegBuild = @"
FFmpeg upstream n8.1.2, vcpkg port $ffmpegVersion, x64-windows, Release, shared libraries and dynamic MSVC CRT.
Extract the upstream source tarball. For a manual build apply the patches in portfile.cmake order with patch -p1.
Use the following options with the local build paths substituted for the three angle-bracket placeholders.

sh ./configure $portableConfigure

The included vcpkg port and triplet also provide the build recipe.
Set up a fresh vcpkg toolchain using https://github.com/microsoft/vcpkg.
"@
    Write-File (Join-Path $ffmpegWork 'BUILD.txt') $ffmpegBuild
    Write-File (Join-Path $buildInfo 'FFmpeg-BUILD.txt') $ffmpegBuild
    $ffmpegZip = Join-Path $sources 'DesktopSceneEngine-FFmpeg-8.1.2-source.zip'
    & tar.exe --format zip -cf $ffmpegZip -C $ffmpegWork .
    if ($LASTEXITCODE -ne 0) { throw 'FFmpeg source archive failed' }

    $jsonVersion = ((Get-Content -LiteralPath (Join-Path $VcpkgTripletDirectory 'share/nlohmann-json/vcpkg.spdx.json') -Raw | ConvertFrom-Json).packages |
        Where-Object name -eq 'nlohmann-json').versionInfo
    Write-File (Join-Path $buildInfo 'component-versions.txt') (@(
        "Qt $qtVersion MSVC 2022 x64 (LGPL-3.0-only libraries/plugins, BSD-3-Clause zh_CN catalog)"
        "Qt Windows entry point $qtVersion (BSD-3-Clause, statically linked)"
        "FFmpeg n8.1.2, port $ffmpegVersion (LGPL-2.1-or-later)"
        "nlohmann/json $jsonVersion (MIT, compiled header-only code)"
        "$($webView.packageId) $($webView.version) (BSD-3-Clause SDK Loader, statically linked x64)"
        "WebView2 SDK package SHA-256: $($webView.sha256)"
        "WebView2 Loader archive SHA-256: $($webView.loaderSha256)"
    ) + $frontendVersions)
    [void]($runtimeImports | Export-Csv -Path (Join-Path $buildInfo 'pe-imports.csv') -NoTypeInformation -Encoding utf8)
    $manifest = Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName | ForEach-Object {
        $relative = $_.FullName.Substring($output.Length + 1).Replace('\', '/')
        '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant(), $relative
    }
    Write-File (Join-Path $output 'SHA256SUMS.txt') $manifest
    Write-Output "Staged $($manifest.Count) verified files in $output"
} finally {
    $scratch = Get-Item -LiteralPath $work
    if ($scratch.FullName -ne [IO.Path]::GetFullPath($work) -or ($scratch.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Release staging scratch directory changed unexpectedly: $work"
    }
    Remove-Item -LiteralPath $scratch.FullName -Recurse -Force
}
