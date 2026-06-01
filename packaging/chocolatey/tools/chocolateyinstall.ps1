$ErrorActionPreference = 'Stop'

$version = '0.2.5'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '74995fa901806860953336b2a8480e9dc47b484baef5fac337130baf79fecde5'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
