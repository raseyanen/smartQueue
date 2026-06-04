class Smartqueue < Formula
  desc "Smart Queue Management System - Docker-based Django app"
  homepage "https://github.com/raseyanen/smartqueue"
  url "https://github.com/raseyanen/smartqueue/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "0a0c59547e4cd7d25bbf2121a89dba3bdd7cd911888fa30626397577cd2d3c3d"
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
