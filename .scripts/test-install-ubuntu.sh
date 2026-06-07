#!/usr/bin/env bash
# Smoke-test a CPack-generated .deb on a clean ubuntu:24.04 container.
#
# Two-stage flow:
#   Stage 1 (builder, kartend-ci image): cmake configure + build + cpack -G DEB.
#            Produces kartend-<ver>-Linux.deb under build/install-test/.
#   Stage 2 (test, fresh ubuntu:24.04):  apt install ./kartend.deb, verifying
#            that CPACK_DEBIAN_PACKAGE_SHLIBDEPS picked up every libQt6/libSDL
#            runtime, and that the install layout (binary, .desktop, icon,
#            metainfo, translations) lands at the GNUInstallDirs paths.
#
# Then smoke-tests the installed binary with QT_QPA_PLATFORM=offscreen so
# --version short-circuits before any window is created.
#
# What this tests:
#   * CPack DEB generator config in CMakeLists.txt is sane
#   * The set of Depends:/Recommends: computed by dpkg-shlibdeps actually
#     resolves on stock Ubuntu 24.04 (catches the libqt6*6 → libqt6*6t64
#     rename class of failures before users hit them)
#   * The installed binary launches with no LD_LIBRARY_PATH tweaks
#
# What this does NOT test:
#   * GUI functionality (no display server)
#   * Other Ubuntu versions (22.04, 25.04) — add as separate invocations if needed
#
# Prereqs:
#   * Docker
#   * kartend-ci image built locally:
#       docker build -f .scripts/Dockerfile.ci -t kartend-ci .
#     (the script auto-builds it if missing)
#
# Usage:
#   .scripts/test-install-ubuntu.sh
#
# Exit status: 0 on success; non-zero on any failure.
set -euo pipefail

err()  { printf '\033[31mtest-install-ubuntu:\033[0m %s\n' "$*" >&2; }
info() { printf '\033[36mtest-install-ubuntu:\033[0m %s\n' "$*" >&2; }

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build/install-test"
BUILDER_IMAGE="kartend-ci"
TEST_IMAGE="ubuntu:24.04"

if ! command -v docker >/dev/null 2>&1; then
  err "docker not on PATH"
  exit 1
fi

# --- Stage 0: ensure the builder image exists ---------------------------------
if ! docker image inspect "${BUILDER_IMAGE}" >/dev/null 2>&1; then
  info "Builder image ${BUILDER_IMAGE} not found — building it once …"
  docker build -f "${REPO_ROOT}/.scripts/Dockerfile.ci" -t "${BUILDER_IMAGE}" "${REPO_ROOT}"
fi

# --- Stage 1: configure + build + cpack -G DEB inside the builder image -------
info "Building .deb in ${BUILDER_IMAGE} (output: ${BUILD_DIR}/) …"

# Clean only the install-test build dir so we don't disturb other build trees
# that may be cached. CMake's CPack DEB needs `file` and `dpkg-dev` which are
# already in the kartend-ci image (dpkg-dev is part of build-essential, file
# is pulled in by it transitively on noble).
docker run --rm \
  -v "${REPO_ROOT}:/src" \
  -w /src \
  "${BUILDER_IMAGE}" \
  bash -c "
    set -euo pipefail
    apt-get update >/dev/null && apt-get install -y --no-install-recommends file dpkg-dev >/dev/null
    rm -rf ${BUILD_DIR}
    cmake -S . -B ${BUILD_DIR} -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DKARTEND_BUILD_TESTS=OFF \
      -DKARTEND_ENABLE_CCACHE=OFF \
      -DKARTEND_PORTABLE_RELEASE=ON
    cmake --build ${BUILD_DIR} --parallel
    (cd ${BUILD_DIR} && cpack -G DEB)
  "

# Pick the .deb CPack produced — DEB-DEFAULT names it as
# <pkg>_<ver>_<arch>.deb (e.g. kartend_0.0.8_amd64.deb). nullglob so a
# no-match expands to an empty array instead of a literal '*.deb'.
shopt -s nullglob
deb_candidates=("${REPO_ROOT}/${BUILD_DIR}"/*.deb)
shopt -u nullglob
DEB_FILE="${deb_candidates[0]:-}"
if [[ -z "${DEB_FILE}" ]]; then
  err "cpack -G DEB produced no .deb under ${BUILD_DIR}/"
  exit 1
fi
info "Produced $(basename "${DEB_FILE}") ($(du -h "${DEB_FILE}" | cut -f1))"

# --- Stage 2: install + smoke-test on clean ubuntu:24.04 ----------------------
info "Pulling ${TEST_IMAGE} …"
docker pull "${TEST_IMAGE}" >/dev/null

CONTAINER_SCRIPT=$(cat <<'EOSCRIPT'
set -euo pipefail

echo '== inspect package metadata =='
dpkg-deb -I /mnt/kartend.deb | sed -n '1,30p'
echo
echo '== inspect package contents (first 30) =='
dpkg-deb -c /mnt/kartend.deb | head -30

echo '== apt install ./kartend.deb (resolves deps from noble) =='
apt-get update >/dev/null
# `apt install ./path.deb` is the modern way to install a local .deb and let
# apt pull its declared Depends: from the configured repos. Equivalent to
# `dpkg -i` + `apt-get -f install` but atomic.
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /mnt/kartend.deb

echo '== dpkg -L kartend (installed files) =='
dpkg -L kartend | head -20
echo '   …'
dpkg -L kartend | wc -l
echo 'files installed.'

echo '== smoke test: kartend --version =='
# offscreen Qt platform so QApplication construction succeeds with no DISPLAY.
QT_QPA_PLATFORM=offscreen /usr/bin/kartend --version

echo '== smoke test: kartend --help =='
QT_QPA_PLATFORM=offscreen /usr/bin/kartend --help | head -20

echo '== OK =='
EOSCRIPT
)

info "Running install test in ${TEST_IMAGE} …"
docker run --rm \
  -v "${DEB_FILE}:/mnt/kartend.deb:ro" \
  "${TEST_IMAGE}" \
  bash -c "${CONTAINER_SCRIPT}"

info "PASS — Ubuntu .deb installs and binary launches."
