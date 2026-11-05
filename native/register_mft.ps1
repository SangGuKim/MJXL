param(
    [switch]$Unregister,
    [switch]$Elevated,
    [string]$ReportPath
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $ReportPath) { $ReportPath = Join-Path $projectRoot 'results\mft-install.json' }
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$admin = [Security.Principal.WindowsPrincipal]::new($identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    if ($Elevated) { throw '관리자 권한으로 실행되지 않았다.' }
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $PSCommandPath + '"'), '-Elevated', '-ReportPath', ('"' + $ReportPath + '"'))
    if ($Unregister) { $arguments += '-Unregister' }
    $process = Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -ArgumentList $arguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode) {
        if (Test-Path -LiteralPath $ReportPath) { Get-Content -LiteralPath $ReportPath -Encoding utf8 }
        throw "MFT 등록 작업이 실패했다: $($process.ExitCode)"
    }
    Get-Content -LiteralPath $ReportPath -Encoding utf8
    $global:LASTEXITCODE = 0
    return
}
$result = [ordered]@{ operation = $(if ($Unregister) { 'unregister' } else { 'install' }); success = $false }
try {
    $classPath = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{D1793512-85F0-458C-94C9-3331913B65A2}\InprocServer32'
    if ($Unregister) {
        $key = Get-Item -LiteralPath $classPath -ErrorAction Stop
        $installed = $key.GetValue('')
        $expectedRoot = [IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'MJXL\MFT')) + '\'
        if (-not [IO.Path]::GetFullPath($installed).StartsWith($expectedRoot, [StringComparison]::OrdinalIgnoreCase)) { throw '예상한 MFT 설치 경로가 아니다.' }
        $arguments = @('/s', '/u', ('"' + $installed + '"'))
    } else {
        $source = Join-Path $projectRoot 'dist\Release\mjxl_mft.dll'
        $hash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
        $versionRoot = Join-Path $env:ProgramFiles ('MJXL\MFT\' + $hash.Substring(0, 16))
        New-Item -ItemType Directory -Path $versionRoot -Force | Out-Null
        $installed = Join-Path $versionRoot 'mjxl_mft.dll'
        if (-not (Test-Path -LiteralPath $installed)) { Copy-Item -LiteralPath $source -Destination $installed }
        if ((Get-FileHash -LiteralPath $installed -Algorithm SHA256).Hash -ne $hash) { throw '설치 파일 해시가 다르다.' }
        $licenses = Join-Path $projectRoot 'dist\Release\licenses'
        if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination $versionRoot -Recurse -Force }
        $arguments = @('/s', ('"' + $installed + '"'))
        $result.sha256 = $hash
    }
    $result.path = $installed
    $process = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru
    $result.exit_code = $process.ExitCode
    if ($process.ExitCode) { throw "regsvr32 종료 코드: $($process.ExitCode)" }
    $result.success = $true
} catch {
    $result.error = $_.Exception.Message
}
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($ReportPath)) | Out-Null
$result | ConvertTo-Json | Set-Content -LiteralPath $ReportPath -Encoding utf8
if (-not $result.success) { exit 1 }

$global:LASTEXITCODE = 0
