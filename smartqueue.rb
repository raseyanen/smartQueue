class Smartqueue < Formula
  desc "Smart Queue Management System - Docker-based Django app"
  homepage "https://github.com/raseyanen/smartqueue"
  url "https://github.com/raseyanen/smartqueue/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "9cfa0c9d234a0b94c303f6224903e37b72e9510326785f57471b8afd2b834a56"
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
           nano #{opt_prefix}/.env.example
        2. Then run:
           smartqueue start
    EOS
  end
end
