$ErrorActionPreference = 'Stop'

$version = '0.2.8'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = 'be5a0fa9f195d5d2bcccefaaabc05f45cd63fee9f8ac3c531593c1bc2273f288'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
