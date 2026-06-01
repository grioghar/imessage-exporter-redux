$ErrorActionPreference = 'Stop'

$version = '0.2.2'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = 'e91a6a876b0eba21b51fa818cedbca47561c2be9ffa5a5ded8fde4c9e483d243'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
