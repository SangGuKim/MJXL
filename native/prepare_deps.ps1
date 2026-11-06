$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '_deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$jxl = Join-Path $dependencyRoot 'libjxl'
if (-not (Test-Path -LiteralPath $jxl)) {
    & git clone --depth 1 --branch v0.11.2 https://github.com/libjxl/libjxl.git $jxl
    if ($LASTEXITCODE) { throw 'Failed to download libjxl.' }
}
$jxlCommit = & git -C $jxl rev-parse HEAD
if ($jxlCommit -ne '332feb17d17311c748445f7ee75c4fb55cc38530') { throw 'Unexpected libjxl version. Existing dependencies were not modified.' }
& git -C $jxl submodule update --init --depth 1 third_party/brotli third_party/highway third_party/skcms
if ($LASTEXITCODE) { throw 'Failed to download libjxl submodules.' }
$samples = Join-Path $dependencyRoot 'windows-samples'
if (-not (Test-Path -LiteralPath $samples)) {
    & git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/Windows-classic-samples.git $samples
    if ($LASTEXITCODE) { throw 'Failed to download the DirectShow base classes.' }
    & git -C $samples sparse-checkout set Samples/Win7Samples/multimedia/directshow/baseclasses
    if ($LASTEXITCODE) { throw 'Failed to select the DirectShow base classes.' }
}

$flac = Join-Path $dependencyRoot 'flac'
if (-not (Test-Path -LiteralPath $flac)) {
    & git clone --depth 1 --branch 1.5.0 https://github.com/xiph/flac.git $flac
    if ($LASTEXITCODE) { throw 'Failed to download libFLAC.' }
}
$flacCommit = & git -C $flac rev-parse HEAD
if ($flacCommit -ne '1507800de4b70e21be71f38caa0d9079d0bc6e45') { throw 'Unexpected libFLAC version. Existing dependencies were not modified.' }

Write-Output 'Dependency preparation completed.'
