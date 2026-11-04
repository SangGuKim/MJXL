param([switch]$Unregister)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$decoder = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\Release\mjxl_decoder.ax'
if (-not (Test-Path -LiteralPath $decoder)) { throw '디코더를 먼저 빌드해야 한다.' }
$arguments = if ($Unregister) { @('/s', '/u', "`"$decoder`"") } else { @('/s', "`"$decoder`"") }
$process = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
if ($process.ExitCode) { throw "디코더 등록 작업이 실패했다: $($process.ExitCode)" }
Write-Output $(if ($Unregister) { '현재 사용자 디코더 등록을 해제했다.' } else { '현재 사용자에게 MJXL 디코더를 등록했다.' })
