$ErrorActionPreference = 'Stop'

$version = '0.2.0'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = 'a427488911403504dc9b02a1400fc66789ea7c43d4a71df599c3ed516815c00b'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
