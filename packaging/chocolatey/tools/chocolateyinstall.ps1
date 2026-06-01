$ErrorActionPreference = 'Stop'

$version = '0.4.1'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '6dc5a8adf9b454cef2bcc96c3e81aad9ccd8761a486f6480c3d67e0249f787b1'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
