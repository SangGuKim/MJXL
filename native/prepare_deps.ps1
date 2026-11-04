$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '_deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$jxl = Join-Path $dependencyRoot 'libjxl'
if (-not (Test-Path -LiteralPath $jxl)) {
    & git clone --depth 1 --branch v0.11.2 https://github.com/libjxl/libjxl.git $jxl
    if ($LASTEXITCODE) { throw 'libjxl 다운로드가 실패했다.' }
}
$jxlCommit = & git -C $jxl rev-parse HEAD
if ($jxlCommit -ne '332feb17d17311c748445f7ee75c4fb55cc38530') { throw '예상하지 않은 libjxl 버전이다. 기존 의존성은 수정하지 않는다.' }
& git -C $jxl submodule update --init --depth 1 third_party/brotli third_party/highway third_party/skcms
if ($LASTEXITCODE) { throw 'libjxl 하위 의존성 다운로드가 실패했다.' }
$samples = Join-Path $dependencyRoot 'windows-samples'
if (-not (Test-Path -LiteralPath $samples)) {
    & git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/Windows-classic-samples.git $samples
    if ($LASTEXITCODE) { throw 'DirectShow 기본 클래스 다운로드가 실패했다.' }
    & git -C $samples sparse-checkout set Samples/Win7Samples/multimedia/directshow/baseclasses
    if ($LASTEXITCODE) { throw 'DirectShow 기본 클래스 선택이 실패했다.' }
}
Write-Output '의존성 준비를 완료했다.'
