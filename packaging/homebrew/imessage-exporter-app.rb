# Homebrew Cask for the desktop GUI (installs the .app from the release .dmg).
#   brew install --cask grioghar/tap/imessage-exporter-app
# The build is unsigned, so add `--no-quarantine` until notarization is set up.
# Update `version` + `sha256` on each release.
cask "imessage-exporter-app" do
  version "0.2.6"
  sha256 "186098075ea1c17fc514dd2e4e038dac25af57c3c3ace7c5419242c910df433c"

  url "https://github.com/grioghar/imessage-exporter-redux/releases/download/v#{version}/iMessage-Exporter-macOS.dmg"
  name "iMessage Exporter"
  desc "Export macOS iMessage/SMS history to TXT, JSON, or HTML"
  homepage "https://github.com/grioghar/imessage-exporter-redux"

  app "iMessage Exporter.app"

  zap trash: [
    "~/Library/Preferences/com.imessage-exporter.iMessage Exporter.plist",
  ]
end
