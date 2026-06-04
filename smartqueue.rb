class Smartqueue < Formula
  desc "Smart Queue Management System - Docker-based Django app"
  homepage "https://github.com/raseyanen/smartqueue"
  url "https://github.com/raseyanen/smartqueue/archive/refs/tags/v1.0.0.tar.gz"
  sha256 "9cfa0c9d234a0b94c303f6224903e37b72e9510326785f57471b8afd2b834a56"
  version "1.0.0"

  depends_on "docker"
  depends_on "docker-compose"

  def install
    # Находим bin/smartqueue где угодно внутри архива
    script = Dir["**/bin/smartqueue"].first
    bin.install script if script
    
    compose = Dir["**/docker-compose.prod.yml"].first
    prefix.install compose if compose
    
    env = Dir["**/.env.example"].first
    prefix.install env if env
  end

  def caveats
    <<~EOS
      First, edit your environment file:
        nano #{opt_prefix}/.env.example
      
      Then start the service:
        smartqueue start
    EOS
  end
end
