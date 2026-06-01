$ErrorActionPreference = 'Stop'

$version = '0.2.1'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '6bf76f6528e264f7173420a404c437c76c5a836bf3945f255c83699ad4b02a1d'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
