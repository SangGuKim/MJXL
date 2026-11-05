param(
    [switch]$Unregister,
    [switch]$AllUsers
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$decoder = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\Release\mjxl_decoder.ax'
if (-not (Test-Path -LiteralPath $decoder)) { throw '디코더를 먼저 빌드해야 한다.' }
$arguments = @('/s')
if ($Unregister) { $arguments += '/u' }
if ($AllUsers) { $arguments += @('/n', '/i:machine') }
$arguments += "`"$decoder`""
$start = @{
    FilePath = "$env:WINDIR\System32\regsvr32.exe"
    ArgumentList = $arguments
    Wait = $true
    PassThru = $true
    WindowStyle = 'Hidden'
}
if ($AllUsers) {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        $start.Verb = 'RunAs'
    }
}
$process = Start-Process @start
if ($process.ExitCode) { throw "디코더 등록 작업이 실패했다: $($process.ExitCode)" }
$scope = if ($AllUsers) { '모든 사용자' } else { '현재 사용자' }
Write-Output $(if ($Unregister) { "$scope 디코더 등록을 해제했다." } else { "$scope 용으로 MJXL 디코더를 등록했다." })
