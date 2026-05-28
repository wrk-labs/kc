class Kc < Formula
  desc "Terminal calendar with CalDAV and Google Calendar sync"
  homepage "https://wrklabs.org/kc"
  url "https://github.com/wrk-labs/kc/archive/refs/tags/v__VERSION__.tar.gz"
  sha256 "__SHA__"
  version "__VERSION__"
  license "GPL-2.0-only"
  head "https://github.com/wrk-labs/kc.git", branch: "master"

  depends_on arch: :arm64
  depends_on :macos
  depends_on "pkg-config" => :build
  depends_on "libical"
  depends_on "ncurses"

  def install
    system "make"
    bin.install "kc"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/kc -v")
  end
end
