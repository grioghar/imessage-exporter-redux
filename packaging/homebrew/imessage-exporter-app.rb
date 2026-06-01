# Homebrew Cask for the desktop GUI (installs the .app from the release .dmg).
#   brew install --cask grioghar/tap/imessage-exporter-app
# The build is unsigned, so add `--no-quarantine` until notarization is set up.
# Update `version` + `sha256` on each release.
cask "imessage-exporter-app" do
  version "0.3.0"
  sha256 "c24ef404f2d6ebfe6461ef6605b7b4f16db511e5562e0b60d8b90519435f12d4"

  url "https://github.com/grioghar/imessage-exporter-redux/releases/download/v#{version}/iMessage-Exporter-macOS.dmg"
  name "iMessage Exporter"
  desc "Export macOS iMessage/SMS history to TXT, JSON, or HTML"
  homepage "https://github.com/grioghar/imessage-exporter-redux"

  app "iMessage Exporter.app"

  zap trash: [
    "~/Library/Preferences/com.imessage-exporter.iMessage Exporter.plist",
  ]
end
