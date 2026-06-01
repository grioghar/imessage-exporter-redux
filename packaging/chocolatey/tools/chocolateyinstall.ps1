$ErrorActionPreference = 'Stop'

$version = '0.2.7'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '15ca65a59f1120ecac2253841f5724a6ecbf3443fd7204e3930b3c6b968c9faa'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
