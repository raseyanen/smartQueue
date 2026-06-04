class Smartqueue < Formula
  desc "Smart Queue Management System - Docker-based Django app"
  homepage "https://github.com/YOUR_USERNAME/smartqueue"
  url "https://github.com/YOUR_USERNAME/smartqueue/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "TODO"  # Заменишь после создания тега
  version "1.0.0"

  depends_on "docker"
  depends_on "docker-compose"

  def install
    bin.install "bin/smartqueue"
    prefix.install "docker-compose.prod.yml"
    prefix.install ".env.example"
  end

  def caveats
    <<~EOS
      First time setup:
        1. Edit your environment file:
           nano #{prefix}/.env.example
        2. Then run:
           smartqueue start

      Config file is stored in ~/.smartqueue/
    EOS
  end
end
