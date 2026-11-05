param(
    [switch]$Unregister,
    [switch]$AllUsers
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
$OutputEncoding = [Console]::OutputEncoding
$decoder = Join-Path (Split-Path -Parent $PSScriptRoot) 'dist\Release\mjxl_decoder.ax'
if (-not (Test-Path -LiteralPath $decoder)) { throw 'Build the decoder first.' }
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
if ($process.ExitCode) { throw "Decoder registration failed with exit code $($process.ExitCode)." }
$scope = if ($AllUsers) { 'all users' } else { 'the current user' }
Write-Output $(if ($Unregister) { "Unregistered the decoder for $scope." } else { "Registered the MJXL decoder for $scope." })
