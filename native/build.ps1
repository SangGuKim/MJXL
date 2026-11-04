$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
if (-not $env:FFMPEG_PATH) { throw 'FFMPEG_PATH 환경변수가 필요하다.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot '_build\vs2026'
$python = Join-Path $env:USERPROFILE 'miniforge3\envs\sgtk\python.exe'
$jxlRoot = Join-Path $env:USERPROFILE 'miniforge3\envs\sgtk\Library'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' -products '*' -property installationPath
if (-not $vsRoot) { throw 'VS2026 설치를 찾을 수 없다.' }
& cmake -S $PSScriptRoot -B $buildRoot -G 'Visual Studio 18 2026' -A x64 -T v145 "-DJXL_ROOT=$jxlRoot"
if ($LASTEXITCODE) { throw 'CMake 구성이 실패했다.' }
& $python -X utf8 (Join-Path $PSScriptRoot 'make_sln.py') $buildRoot
if ($LASTEXITCODE) { throw '.sln 생성이 실패했다.' }
& (Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe') (Join-Path $projectRoot 'MJXL.sln') /p:Configuration=Release /p:Platform=x64 /m:4 /verbosity:minimal /consoleloggerparameters:ErrorsOnly /nologo
if ($LASTEXITCODE) { throw 'VS2026 빌드가 실패했다.' }
$destination = Join-Path $projectRoot 'dist\Release'
Copy-Item -Path (Join-Path $env:FFMPEG_PATH 'bin\*.dll') -Destination $destination
Copy-Item -LiteralPath (Join-Path $env:FFMPEG_PATH 'LICENSE.txt') -Destination (Join-Path $destination 'FFmpeg-LICENSE.txt')
$licenseRoot = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
foreach ($entry in @(@('libjxl\LICENSE','JPEG-XL-LICENSE.txt'), @('libjxl\PATENTS','JPEG-XL-PATENTS.txt'), @('libjxl\third_party\brotli\LICENSE','Brotli-LICENSE.txt'), @('libjxl\third_party\highway\LICENSE','Highway-LICENSE.txt'), @('libjxl\third_party\skcms\LICENSE','skcms-LICENSE.txt'), @('windows-samples\LICENSE','DirectShow-samples-LICENSE.txt'))) {
    $licenseSource = Join-Path $projectRoot "_deps\$($entry[0])"
    if (Test-Path -LiteralPath $licenseSource) { Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $licenseRoot $entry[1]) }
}
Write-Output "빌드 완료: $destination"
