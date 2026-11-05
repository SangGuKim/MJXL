# Installs the MJXL decoder and tools on this PC so they no longer depend on the source drive.
# Text is ASCII only because Windows PowerShell 5 reads scripts without a BOM as the ANSI code page.
#
#   install.ps1                    Install to %LOCALAPPDATA%\MJXL for the current user
#   install.ps1 -DecoderOnly       Install only the playback decoder, without mjxl.exe
#   install.ps1 -Destination DIR   Install to another folder
#   install.ps1 -Uninstall         Unregister the decoder and remove the installed files
param(
    [string]$Destination = (Join-Path $env:LOCALAPPDATA 'MJXL'),
    [switch]$DecoderOnly,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'

$decoderName = 'mjxl_decoder.ax'
$decoderClass = 'HKCU:\Software\Classes\CLSID\{6E05CB1D-BFC9-4AEB-AC70-4F35BB862964}\InprocServer32'
$filterCacheKey = 'HKCU:\Software\Microsoft\Multimedia\ActiveMovie\Filter Cache64'

if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'The MJXL decoder is built for 64-bit Windows only.'
}
# A 32-bit shell must reach the 64-bit regsvr32 through SysNative.
$systemFolder = if ([Environment]::Is64BitProcess) { 'System32' } else { 'SysNative' }
$regsvr32 = Join-Path $env:WINDIR "$systemFolder\regsvr32.exe"

function Invoke-Regsvr32([string[]]$Arguments) {
    $process = Start-Process -FilePath $regsvr32 -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode) { throw "regsvr32 failed with exit code $($process.ExitCode)." }
}

# DirectShow keeps its own list of filters per user. A stale list hides a newly registered filter.
function Clear-FilterCache {
    if (Test-Path -LiteralPath $filterCacheKey) {
        Remove-Item -LiteralPath $filterCacheKey -Recurse -Force
    }
}

# Uses .NET because Get-FileHash is missing when PowerShell 7 starts Windows PowerShell with its module path.
function Get-Sha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream))
    } finally {
        $stream.Dispose()
        $sha.Dispose()
    }
}

function Copy-Verified([string]$Source, [string]$Folder) {
    $target = Join-Path $Folder (Split-Path -Leaf $Source)
    if ((Test-Path -LiteralPath $target) -and (Get-Sha256 $target) -eq (Get-Sha256 $Source)) {
        return
    }
    try {
        Copy-Item -LiteralPath $Source -Destination $target -Force
    } catch {
        throw "Cannot replace $target. Close players and Explorer windows showing these videos, then try again. $($_.Exception.Message)"
    }
    if ((Get-Sha256 $target) -ne (Get-Sha256 $Source)) {
        throw "Copied file differs from its source: $target"
    }
}

# Follows the import names in the binaries so that only the FFmpeg DLLs in use are copied.
function Get-RequiredLibraries([string]$Program, [string]$Folder) {
    $encoding = [Text.Encoding]::GetEncoding(28591)
    $candidates = Get-ChildItem -LiteralPath $Folder -Filter '*.dll'
    $found = @{}
    $pending = New-Object System.Collections.Queue
    $pending.Enqueue($Program)
    while ($pending.Count) {
        $text = $encoding.GetString([IO.File]::ReadAllBytes($pending.Dequeue()))
        foreach ($candidate in $candidates) {
            if (-not $found.ContainsKey($candidate.Name) -and $text.IndexOf($candidate.Name, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $found[$candidate.Name] = $candidate.FullName
                $pending.Enqueue($candidate.FullName)
            }
        }
    }
    return $found.Values
}

$installedDecoder = Join-Path $Destination $decoderName
$manifest = Join-Path $Destination 'mjxl-install.txt'

if ($Uninstall) {
    if (Test-Path -LiteralPath $installedDecoder) {
        Invoke-Regsvr32 @('/s', '/u', "`"$installedDecoder`"")
    }
    Clear-FilterCache
    # Remove only the files this script recorded, so that a wrong folder cannot lose other files.
    if (Test-Path -LiteralPath $manifest) {
        foreach ($name in (Get-Content -LiteralPath $manifest)) {
            $file = Join-Path $Destination $name
            if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file -Force }
        }
        Remove-Item -LiteralPath $manifest -Force
        $licenses = Join-Path $Destination 'licenses'
        if ((Test-Path -LiteralPath $licenses) -and -not (Get-ChildItem -LiteralPath $licenses -Force)) { Remove-Item -LiteralPath $licenses -Force }
        if (-not (Get-ChildItem -LiteralPath $Destination -Force)) { Remove-Item -LiteralPath $Destination -Force }
        Write-Output "Uninstalled MJXL from $Destination"
    } else {
        Write-Output "No MJXL install record in $Destination; no files were removed."
    }
    return
}

$sourceFolder = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\dist\Release'))
$sourceDecoder = Join-Path $sourceFolder $decoderName
if (-not (Test-Path -LiteralPath $sourceDecoder)) { throw "Build the project first: $sourceDecoder was not found." }
if ([IO.Path]::GetFullPath($Destination).TrimEnd('\') -eq $sourceFolder.TrimEnd('\')) { throw 'Destination must differ from the build folder.' }

New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Copy-Verified $sourceDecoder $Destination
$installedNames = New-Object System.Collections.ArrayList
[void]$installedNames.Add($decoderName)

if (-not $DecoderOnly) {
    $program = Join-Path $sourceFolder 'mjxl.exe'
    if (-not (Test-Path -LiteralPath $program)) { throw "Build the project first: $program was not found." }
    Copy-Verified $program $Destination
    [void]$installedNames.Add('mjxl.exe')
    foreach ($library in (Get-RequiredLibraries $program $sourceFolder)) {
        Copy-Verified $library $Destination
        [void]$installedNames.Add((Split-Path -Leaf $library))
    }
    $ffmpegLicense = Join-Path $sourceFolder 'FFmpeg-LICENSE.txt'
    if (Test-Path -LiteralPath $ffmpegLicense) {
        Copy-Verified $ffmpegLicense $Destination
        [void]$installedNames.Add('FFmpeg-LICENSE.txt')
    }
    $licenses = Join-Path $sourceFolder 'licenses'
    if (Test-Path -LiteralPath $licenses) {
        New-Item -ItemType Directory -Path (Join-Path $Destination 'licenses') -Force | Out-Null
        foreach ($license in (Get-ChildItem -LiteralPath $licenses -File)) {
            Copy-Verified $license.FullName (Join-Path $Destination 'licenses')
            [void]$installedNames.Add("licenses\$($license.Name)")
        }
    }
}
$installedNames | Set-Content -LiteralPath $manifest -Encoding Ascii

Invoke-Regsvr32 @('/s', "`"$installedDecoder`"")
$registered = (Get-ItemProperty -LiteralPath $decoderClass).'(default)'
if ($registered -ne $installedDecoder) { throw "Decoder registration points to another file: $registered" }
Clear-FilterCache

Write-Output "Installed the MJXL decoder for the current user: $installedDecoder"
if (-not $DecoderOnly) { Write-Output "Command line tool: $(Join-Path $Destination 'mjxl.exe')" }
Write-Output 'Reopen PotPlayer (64-bit) to play MJXL AVI files.'
