$ErrorActionPreference = 'Stop'

$tag     = 'v0.5.2.202606020119'
$url     = "https://github.com/grioghar/imessage-exporter-redux/releases/download/$tag/iMessage-Exporter-Setup.exe"

$packageArgs = @{
  packageName    = 'imessage-exporter'
  fileType       = 'exe'
  url64bit       = $url
  silentArgs     = '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-'
  validExitCodes = @(0)
  softwareName   = 'iMessage Exporter*'
  checksum64     = '77c8f2bf21774f891af196827d4002ea6f0ba4527696504919fd0121766f6aa5'
  checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
