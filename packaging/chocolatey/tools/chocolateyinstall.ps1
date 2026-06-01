$ErrorActionPreference = 'Stop'

$version = '0.2.4'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = 'ae7b8458f2abc5a8b07ce3f256acebbf65aeface0cda8c81624c9a8da0dd6997'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
