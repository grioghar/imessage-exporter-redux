$ErrorActionPreference = 'Stop'

$version = '0.3.0'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/v$version/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '4ae136c443976e14d601537e8fd1e3cf99e960ae7e6c848f4cef8c37e1568a66'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
