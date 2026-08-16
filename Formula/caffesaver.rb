class Caffesaver < Formula
  desc "Animated terminal screensavers & generative art with C engine and caffeinate anti-sleep"
  homepage "https://github.com/Duccioo/caffesaver"
  url "https://github.com/Duccioo/caffesaver/archive/refs/tags/v0.0.28.tar.gz"
  head "https://github.com/Duccioo/caffesaver.git", branch: "main"
  license "MIT"

  depends_on "bash"

  def install
    # Install all project files into libexec to keep directory structure intact
    libexec.install Dir["*"]

    # Ensure scripts have execute permissions
    chmod 0755, libexec/"screensaver.sh"

    # Create wrapper script in bin/caffesaver
    (bin/"caffesaver").write_env_script "#{libexec}/screensaver.sh", {}
  end

  def caveats
    <<~EOS
      ☕ caffesaver has been installed!
      Run it by typing:
        caffesaver             # Interactive menu
        caffesaver -r          # Random screensaver
        caffesaver -m matrix   # Run Matrix directly
        caffesaver -m rorschach-led
    EOS
  end

  test do
    assert_match "caffesaver", shell_output("#{bin}/caffesaver --version")
  end
end
