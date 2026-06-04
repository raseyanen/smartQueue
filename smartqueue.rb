class Smartqueue < Formula
  desc "Smart Queue Management System - Docker-based Django app"
  homepage "https://github.com/raseyanen/smartqueue"
  url "https://github.com/raseyanen/smartqueue/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "af0e4297ed1d4d5882345967d90629c10ec2ace44c646dd6b0c27a1843cc03c8"  # Заменишь после создания тега
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
