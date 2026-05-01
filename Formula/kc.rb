class Kc < Formula
  desc "Terminal calendar with CalDAV and Google Calendar sync"
  homepage "https://wrklabs.org/kc"
  version "__VERSION__"
  license "GPL-2.0-only"

  depends_on arch: :arm64
  depends_on :macos
  depends_on "libical"
  depends_on "ncurses"

  url "https://brew.wrklabs.org/dist/kc/kc-__VERSION__-darwin-arm64.tar.gz"
  sha256 "__SHA_ARM64__"

  def install
    bin.install "kc"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/kc -v")
  end
end
