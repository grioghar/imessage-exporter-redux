$ErrorActionPreference = 'Stop'

$version = '0.2.6'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '8e2acdaef0be1dc78244897d66a8aa9f04daff7b82d5da76f0a58a4201bb0578'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
