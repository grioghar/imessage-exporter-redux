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
  # Required for the Chocolatey community feed — set to the released
  # Setup.exe SHA-256 (Get-FileHash) at publish time:
  # checksum64     = '<SHA256 OF iMessage-Exporter-Setup.exe>'
  # checksumType64 = 'sha256'
}

Install-ChocolateyPackage @packageArgs
