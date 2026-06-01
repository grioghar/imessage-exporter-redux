$ErrorActionPreference = 'Stop'

# Drive the Inno Setup uninstaller registered for the app.
[array]$keys = Get-UninstallRegistryKey -SoftwareName 'iMessage Exporter*'
if ($keys.Count -eq 1) {
  $keys | ForEach-Object {
    Uninstall-ChocolateyPackage `
      -PackageName 'imessage-exporter' `
      -FileType 'exe' `
      -SilentArgs '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' `
      -File "$($_.UninstallString)"
  }
} elseif ($keys.Count -eq 0) {
  Write-Warning 'iMessage Exporter is not installed (nothing to uninstall).'
} else {
  Write-Warning "Found $($keys.Count) matching installs; uninstall manually."
}
