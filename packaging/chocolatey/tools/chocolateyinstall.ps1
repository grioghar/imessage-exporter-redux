$ErrorActionPreference = 'Stop'

$version = '0.2.3'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = 'd03f7a5c0af32a36ffc75c85563dd813191505a61a79561d276555e2adfa6cba'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
