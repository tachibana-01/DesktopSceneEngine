$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$phaseRoot = Split-Path -Parent $PSScriptRoot
$dependencyLock = Get-Content -LiteralPath (Join-Path $phaseRoot 'dependency-lock.json') -Raw |
    ConvertFrom-Json
$sdk = $dependencyLock.webView2
$downloadRoot = Join-Path $phaseRoot '.deps\downloads'
$packagePath = Join-Path $downloadRoot "microsoft.web.webview2.$($sdk.version).nupkg"
$sdkRoot = Join-Path $phaseRoot ".deps\Microsoft.Web.WebView2.$($sdk.version)"

New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
if (-not (Test-Path -LiteralPath $packagePath)) {
    Invoke-WebRequest -Uri $sdk.source -OutFile $packagePath
}

$actualPackageHash = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
if ($actualPackageHash -ne $sdk.sha256) {
    throw "WebView2 package SHA256 mismatch: $packagePath"
}

if (-not (Test-Path -LiteralPath $sdkRoot)) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($packagePath, $sdkRoot)
}

$requiredFiles = @(
    @{ Path = 'build\native\include\WebView2.h'; Hash = $sdk.headerSha256 },
    @{ Path = 'build\native\x64\WebView2LoaderStatic.lib'; Hash = $sdk.loaderSha256 }
)
foreach ($required in $requiredFiles) {
    $artifactPath = Join-Path $sdkRoot $required.Path
    if ((Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash -ne $required.Hash) {
        throw "WebView2 SDK artifact SHA256 mismatch: $artifactPath"
    }
}

Write-Output "WebView2 SDK $($sdk.version) x64 static loader: READY"
Write-Output "Package SHA256: $($sdk.sha256)"
Write-Output "SDK root: $sdkRoot"
