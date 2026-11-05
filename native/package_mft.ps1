param([string]$Version = '0.1.0.0', [switch]$Sign)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new()
$root = Split-Path -Parent $PSScriptRoot
$kits = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Windows Kits\10\bin'
$sdk = Get-ChildItem -LiteralPath $kits -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'x64\makeappx.exe') } | Sort-Object Name -Descending | Select-Object -First 1
if (-not $sdk) { throw 'The Windows SDK makeappx tool is required.' }
$stage = Join-Path $root ('_build\mft-appx-' + $Version)
New-Item -ItemType Directory -Path $stage -Force | Out-Null
[xml]$manifest = Get-Content (Join-Path $PSScriptRoot 'appx\AppxManifest.xml') -Raw -Encoding utf8
$manifest.Package.Identity.Version = $Version
$manifest.Save((Join-Path $stage 'AppxManifest.xml'))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'appx\Assets') -Destination $stage -Recurse -Force
Copy-Item -LiteralPath (Join-Path $root 'dist\Release\mjxl_mft.dll') -Destination $stage -Force
$licenses = Join-Path $root 'dist\Release\licenses'
if (Test-Path -LiteralPath $licenses) { Copy-Item -LiteralPath $licenses -Destination $stage -Recurse -Force }
$output = Join-Path $root ('dist\MJXL.MediaExtension_' + $Version + '_x64.msix')
& (Join-Path $sdk.FullName 'x64\makeappx.exe') pack /d $stage /p $output /o
if ($LASTEXITCODE) { throw 'Failed to create the media extension package.' }
$result = [ordered]@{ package = $output; signed = $false; version = $Version }
if ($Sign) {
    $cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=MJXL Local Development' -and $_.NotAfter -gt (Get-Date).AddDays(1) -and $_.HasPrivateKey } | Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $cert) {
        $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=MJXL Local Development' -FriendlyName 'MJXL local package development' -CertStoreLocation Cert:\CurrentUser\My -NotAfter (Get-Date).AddDays(30) -KeyExportPolicy NonExportable
    }
    $cer = Join-Path $root 'dist\MJXL.LocalDevelopment.cer'
    Export-Certificate -Cert $cert -FilePath $cer -Force | Out-Null
    & (Join-Path $sdk.FullName 'x64\signtool.exe') sign /fd SHA256 /sha1 $cert.Thumbprint /s My $output
    if ($LASTEXITCODE) { throw 'Package signing failed.' }
    $result.signed = $true
    $result.certificate = $cer
    $result.thumbprint = $cert.Thumbprint
    $result.certificate_expires = $cert.NotAfter.ToString('o')
}
$result | ConvertTo-Json | Set-Content (Join-Path $root 'results\mft-package.json') -Encoding utf8
$result
