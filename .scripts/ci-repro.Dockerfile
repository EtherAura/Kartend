FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Mirror the build.yml install lists for build / build-no-zstd / sanitizers /
# thread-sanitizer / maintenance-check / coverage. Installing the union here
# keeps the image small enough (one base layer) without needing per-job
# variants. Debug tools (gdb, strace) are extra so an interactive --shell can
# actually root-cause SIGILLs / TSan races.
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates \
      cmake ninja-build ccache \
      clang clang-tidy clang-format-19 cppcheck \
      gcc g++ lld \
      qt6-base-dev qt6-multimedia-dev libqt6sql6-sqlite \
      libzstd-dev \
      lcov jq \
      pulseaudio pulseaudio-utils \
      git \
      gdb strace \
      sudo \
    && ln -sf /usr/bin/clang-format-19 /usr/local/bin/clang-format \
    && rm -rf /var/lib/apt/lists/*

# Match host UID/GID so bind-mounted source files don't end up root-owned.
# Ubuntu 24.04 ships a default `ubuntu` user at UID 1000; if the host UID
# happens to match (the common case), reuse that user instead of trying to
# add a colliding account. Otherwise create a fresh `dev` user.
ARG HOST_UID=1000
ARG HOST_GID=1000
RUN existing="$(getent passwd "$HOST_UID" | cut -d: -f1 || true)"; \
    if [ -n "$existing" ]; then \
      usermod -l dev -d /home/dev -m "$existing" 2>/dev/null || true; \
      groupmod -n dev "$(getent group "$HOST_GID" | cut -d: -f1)" 2>/dev/null || true; \
    else \
      groupadd -g "$HOST_GID" dev; \
      useradd -m -u "$HOST_UID" -g "$HOST_GID" -s /bin/bash dev; \
    fi \
    && usermod -aG sudo dev \
    && echo 'dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/dev

USER dev
WORKDIR /work
