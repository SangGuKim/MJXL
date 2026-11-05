param(
    [string]$BuildDirectory = '_build\windows-vs2026',
    [switch]$ConfigureOnly
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$ffmpegRoot = if ($env:FFMPEG_SDK) { $env:FFMPEG_SDK } else { $env:FFMPEG_PATH }
if (-not $ffmpegRoot) { throw 'Set FFMPEG_SDK or FFMPEG_PATH to the FFmpeg SDK directory.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot $BuildDirectory
$python = Join-Path $env:USERPROFILE 'miniforge3\envs\sgtk\python.exe'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = & $vswhere -latest -version '[18.0,19.0)' -products '*' -property installationPath
if (-not $vsRoot) { throw 'Visual Studio 2026 was not found.' }
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' }
$cache = Join-Path $buildRoot 'CMakeCache.txt'
if (Test-Path -LiteralPath $cache) {
    $homeEntry = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($homeEntry) {
        $cachedRoot = [IO.Path]::GetFullPath($homeEntry.Matches[0].Groups[1].Value).TrimEnd('\', '/')
        if ($cachedRoot -ne [IO.Path]::GetFullPath($projectRoot).TrimEnd('\', '/')) {
            throw "The existing CMake cache uses another source directory ($cachedRoot). Select a new folder with -BuildDirectory."
        }
    }
}
Write-Output '[1/4] Configuring the CMake project.'
& $cmake -S $projectRoot -B $buildRoot -G 'Visual Studio 18 2026' -A x64 -T v145 "-DFFMPEG_ROOT=$ffmpegRoot"
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
Write-Output '[2/4] Generating the Visual Studio solution in the build folder.'
& $python -X utf8 (Join-Path $PSScriptRoot 'make_sln.py') $buildRoot
if ($LASTEXITCODE) { throw 'Solution generation failed.' }
$solution = Join-Path $buildRoot 'mjxl.sln'
if ($ConfigureOnly) {
    Write-Output "Project generation completed: $solution (compilation was not requested)."
    return
}
Write-Output '[3/4] Building all Release targets. The first build also compiles libjxl and its dependencies.'
& (Join-Path $vsRoot 'MSBuild\Current\Bin\MSBuild.exe') $solution /p:Configuration=Release /p:Platform=x64 /m:4 /verbosity:minimal /nologo
if ($LASTEXITCODE) { throw 'The Visual Studio 2026 build failed.' }
$destination = Join-Path $projectRoot 'dist\Release'
Write-Output '[4/4] Copying runtime DLLs and licenses.'
Copy-Item -Path (Join-Path $ffmpegRoot 'bin\*.dll') -Destination $destination
$ffmpegLicense = @('LICENSE.txt', 'LICENSE') | ForEach-Object { Join-Path $ffmpegRoot $_ } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $ffmpegLicense) { throw 'The FFmpeg license file was not found.' }
Copy-Item -LiteralPath $ffmpegLicense -Destination (Join-Path $destination 'FFmpeg-LICENSE.txt')
$licenseRoot = Join-Path $destination 'licenses'
New-Item -ItemType Directory -Path $licenseRoot -Force | Out-Null
foreach ($entry in @(@('libjxl\LICENSE','JPEG-XL-LICENSE.txt'), @('libjxl\PATENTS','JPEG-XL-PATENTS.txt'), @('libjxl\third_party\brotli\LICENSE','Brotli-LICENSE.txt'), @('libjxl\third_party\highway\LICENSE','Highway-LICENSE.txt'), @('libjxl\third_party\skcms\LICENSE','skcms-LICENSE.txt'), @('windows-samples\LICENSE','DirectShow-samples-LICENSE.txt'))) {
    $licenseSource = Join-Path $projectRoot "_deps\$($entry[0])"
    if (Test-Path -LiteralPath $licenseSource) { Copy-Item -LiteralPath $licenseSource -Destination (Join-Path $licenseRoot $entry[1]) }
}
Write-Output "Build completed: $destination"
