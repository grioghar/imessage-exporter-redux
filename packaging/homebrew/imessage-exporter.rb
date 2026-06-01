# Homebrew formula for the CLI. Publish via a tap, e.g.:
#   brew tap grioghar/tap https://github.com/grioghar/homebrew-tap
#   brew install grioghar/tap/imessage-exporter
# On each release, update `url`/`version` and run `brew fetch` to get the sha256.
class ImessageExporter < Formula
  desc "Export macOS iMessage/SMS history to TXT, JSON, or HTML"
  homepage "https://github.com/grioghar/imessage-exporter-redux"
  url "https://github.com/grioghar/imessage-exporter-redux/archive/refs/tags/v0.2.8.tar.gz"
  sha256 "5f10f92adfaa06ced6e311a0ec27a0238cbfde0eedcdade89b619c91bbe7d740"
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
