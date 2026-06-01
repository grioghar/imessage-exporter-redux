# Homebrew formula for the CLI. Publish via a tap, e.g.:
#   brew tap grioghar/tap https://github.com/grioghar/homebrew-tap
#   brew install grioghar/tap/imessage-exporter
# On each release, update `url`/`version` and run `brew fetch` to get the sha256.
class ImessageExporter < Formula
  desc "Export macOS iMessage/SMS history to TXT, JSON, or HTML"
  homepage "https://github.com/grioghar/imessage-exporter-redux"
  url "https://github.com/grioghar/imessage-exporter-redux/archive/refs/tags/v0.2.3.tar.gz"
  sha256 "9350ad2e04531e9bb2228743b5356200eccb998d06dfdd6fa83bd9a2e947ff54"
  license "MIT"
  head "https://github.com/grioghar/imessage-exporter-redux.git", branch: "main"

  depends_on "cmake" => :build
  on_linux do
    depends_on "sqlite"
  end

  def install
    system "cmake", "-S", ".", "-B", "build", "-DCMAKE_BUILD_TYPE=Release"
    system "cmake", "--build", "build", "--target", "imessage-exporter"
    bin.install "build/imessage-exporter"
    man1.install "man/imessage-exporter.1"
  end

  test do
    assert_match "imessage-exporter", shell_output("#{bin}/imessage-exporter --version")
  end
end
