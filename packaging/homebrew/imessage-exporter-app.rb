# Homebrew Cask for the desktop GUI (installs the .app from the release .dmg).
#   brew install --cask grioghar/tap/imessage-exporter-app
# The build is unsigned, so add `--no-quarantine` until notarization is set up.
# Update `version` + `sha256` on each release.
cask "imessage-exporter-app" do
  version "0.2.5"
  sha256 "1f7b6368a5a40250d3b0ecf0d3ecd34c7a74d1d3b727cc4d91f2041567a09c73"

  url "https://github.com/grioghar/imessage-exporter-redux/releases/download/v#{version}/iMessage-Exporter-macOS.dmg"
  name "iMessage Exporter"
  desc "Export macOS iMessage/SMS history to TXT, JSON, or HTML"
  homepage "https://github.com/grioghar/imessage-exporter-redux"

  app "iMessage Exporter.app"

  zap trash: [
    "~/Library/Preferences/com.imessage-exporter.iMessage Exporter.plist",
  ]
end
