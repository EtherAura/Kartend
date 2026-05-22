#!/usr/bin/env bash
# Smoke-test the shipped Arch PKGBUILD on a clean archlinux:latest container.
#
# What this tests:
#   * The PKGBUILD's `source=` URL is reachable and the sha256sum still matches
#   * The declared `depends=` resolve via pacman on current Arch
#   * `makepkg -si` builds the project and installs the resulting package
#   * The installed /usr/bin/kartend launches (--version short-circuits before
#     any window is created; QT_QPA_PLATFORM=offscreen avoids needing xvfb)
#
# What this does NOT test:
#   * Current local code — PKGBUILD pulls the released tarball from GitHub
#     (v$pkgver, currently v0.0.8). Bump VERSION and cut a release before this
#     reflects today's main.
#   * GUI functionality — no display server, no GPU
#
# Usage:
#   .scripts/test-install-arch.sh
#
# Exit status:
#   0 on success; non-zero on any step failure (pacman, makepkg, or smoke test).
set -euo pipefail

err()  { printf '\033[31mtest-install-arch:\033[0m %s\n' "$*" >&2; }
info() { printf '\033[36mtest-install-arch:\033[0m %s\n' "$*" >&2; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKGBUILD_PATH="${REPO_ROOT}/packaging/PKGBUILD"

if [[ ! -f "${PKGBUILD_PATH}" ]]; then
  err "PKGBUILD not found at ${PKGBUILD_PATH}"
  exit 1
fi

if ! command -v docker >/dev/null 2>&1; then
  err "docker not on PATH"
  exit 1
fi

info "Pulling archlinux:latest …"
docker pull archlinux:latest >/dev/null

# Inline script run inside the container. makepkg refuses to run as root, so
# we create a `builder` user with passwordless sudo (needed for `makepkg -si`
# to invoke pacman for declared deps and to install the resulting .pkg.tar.zst).
CONTAINER_SCRIPT=$(cat <<'EOSCRIPT'
set -euo pipefail

echo '== bootstrap base-devel + git =='
pacman -Syu --noconfirm --needed base-devel git sudo
# Qt's offscreen platform plugin lives in qt6-base, which is a runtime dep of
# the package — pacman will pull it during install. No extra setup needed here.

echo '== create non-root builder user =='
useradd -m -G wheel -s /bin/bash builder
echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder

echo '== copy PKGBUILD into builder home =='
install -d -o builder -g builder /home/builder/build
install -o builder -g builder -m 644 /mnt/PKGBUILD /home/builder/build/PKGBUILD

echo '== makepkg -si (download, build, install) =='
# --skipinteg would skip the sha256 check; we keep it on so a tarball mismatch
# is a hard failure. --noconfirm so pacman doesn't block on prompts.
sudo -u builder bash -lc 'cd ~/build && makepkg -si --noconfirm'

echo '== installed file manifest =='
pacman -Ql kartend | head -20
echo '   …'
pacman -Ql kartend | wc -l
echo 'files installed.'

echo '== smoke test: kartend --version =='
# offscreen Qt platform so QApplication construction succeeds with no DISPLAY.
# --version is handled by QCommandLineParser::process() and exits before any
# QMainWindow is created.
QT_QPA_PLATFORM=offscreen /usr/bin/kartend --version

echo '== smoke test: kartend --help =='
QT_QPA_PLATFORM=offscreen /usr/bin/kartend --help | head -20

echo '== OK =='
EOSCRIPT
)

info "Running install test in archlinux:latest …"
docker run --rm \
  -v "${PKGBUILD_PATH}:/mnt/PKGBUILD:ro" \
  archlinux:latest \
  bash -c "${CONTAINER_SCRIPT}"

info "PASS — Arch PKGBUILD installs and binary launches."
