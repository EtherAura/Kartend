#!/usr/bin/env bash
# Cross-compile Kartend to Windows (x86_64) from a Linux host.
#
# Pairs with .scripts/Dockerfile.windows-cross. Produces a runnable
# kartend.exe + windeployqt-equivalent DLL/plugin bundle next to it,
# then zips the lot at the repo root.
#
# Toolchain: MinGW-w64 + Qt6 6.8.x via Fedora 41's mingw64-* packages
# inside a Docker container. First run builds the image (~5 min); each
# subsequent run reuses it.
#
# Caveats — read before treating the output as a release artifact:
#   * MinGW-GCC, not MSVC. The release pipeline uses MSVC via CI
#     (build.yml's windows-preview-build job). Use this for fast
#     Windows-portability iteration locally without the CI round-trip;
#     don't ship MinGW-cross binaries to users without explicit testing.
#   * No Qt6Keychain (Fedora's mingw64 set only carries the Qt5 build),
#     so scraper credentials fall back to plaintext-INI storage in the
#     resulting kartend.exe.
#   * The exe is unsigned — Windows SmartScreen will warn on first run.
#
# Output layout (all under build/ so the regular build/ gitignore covers
# everything; docker already runs as the invoking user so files are
# host-owned):
#   build/windows-mingw/                          cross-compile build tree
#   build/windows-mingw-dist/                     staged exe + DLLs + Qt plugins
#   build/kartend-windows-x64-mingw-preview.zip   final artifact
#
# Usage:
#   .scripts/build-windows-cross.sh           # configure → build → stage → zip
#   .scripts/build-windows-cross.sh --clean   # also wipe build/windows-mingw/
#   .scripts/build-windows-cross.sh --rebuild-image  # force a fresh docker image build
#   .scripts/build-windows-cross.sh -h        # show this header

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE_TAG="kartend-windows-cross:latest"
DOCKERFILE="${REPO_ROOT}/.scripts/Dockerfile.windows-cross"
# All artifacts go under build/ so they fall under the existing build/
# gitignore rule (no separate /dist-windows-mingw/, /kartend-…zip rules
# needed). The previous layout dropped these at the repo root, leaving
# stale root-owned dirs that needed sudo to delete. Matches the regular
# build.sh convention (build/ninja-release/, build/ninja-debug/, …).
BUILD_DIR_REL="build/windows-mingw"
DIST_DIR_REL="build/windows-mingw-dist"
ZIP_NAME="build/kartend-windows-x64-mingw-preview.zip"

CLEAN=0
REBUILD_IMAGE=0
for arg in "$@"; do
  case "$arg" in
    --clean) CLEAN=1 ;;
    --rebuild-image) REBUILD_IMAGE=1 ;;
    -h|--help)
      # Print the header comment block as help.
      awk '/^# /{ sub(/^# ?/,""); print; next } /^$/{ exit }' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "error: unknown arg: $arg (try --help)" >&2
      exit 2
      ;;
  esac
done

# 1. Build or reuse the cross-compile image.
if [ "$REBUILD_IMAGE" -eq 1 ] || ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
  echo "[*] Building cross-compile image $IMAGE_TAG (first run ~5 min)"
  docker build -t "$IMAGE_TAG" -f "$DOCKERFILE" "${REPO_ROOT}/.scripts/"
else
  echo "[✓] Using cached image $IMAGE_TAG"
fi

# 2. Configure + build inside the container.
#    mingw64-cmake is Fedora's wrapper around CMake that sets
#    CMAKE_TOOLCHAIN_FILE, PKG_CONFIG_PATH, and the cross-prefix env vars
#    for the mingw64 sysroot. Use Ninja: the Visual Studio generator
#    isn't available cross-host anyway, and Ninja gives cleaner AUTOUIC
#    dependency ordering than Unix Makefiles.
echo "[*] Cross-compiling (mingw64-cmake + Ninja, Release, portable flags)"
# --user maps the container's primary user to the host invoker so the
# build artifacts under build-windows-mingw/ end up owned by the
# invoking user instead of root, which would otherwise need sudo to
# clean up. Falls back to running as the container's default user when
# id is unavailable (unlikely on a Linux dev host).
DOCKER_USER_ARG="$(id -u):$(id -g)"
docker run --rm \
  --user "$DOCKER_USER_ARG" \
  -v "${REPO_ROOT}:/src" \
  "$IMAGE_TAG" \
  bash -c "
    set -e
    $( [ "$CLEAN" -eq 1 ] && echo "rm -rf /src/${BUILD_DIR_REL}" )
    mingw64-cmake -G Ninja -S /src -B /src/${BUILD_DIR_REL} \\
      -DCMAKE_BUILD_TYPE=Release \\
      -DKARTEND_BUILD_TESTS=OFF \\
      -DKARTEND_ENABLE_CCACHE=OFF \\
      -DKARTEND_PORTABLE_RELEASE=ON
    cmake --build /src/${BUILD_DIR_REL} --parallel
  "

# 3. Stage DLLs + Qt plugins next to the exe, then zip.
#    Recursive objdump-based import walk copies every transitive DLL from
#    /usr/x86_64-w64-mingw32/sys-root/mingw/bin/. Plugin dirs (platforms,
#    sqldrivers, imageformats, multimedia, styles, tls, iconengines,
#    networkinformation) are copied wholesale where present, then their
#    own transitive imports are walked too — windeployqt does the same
#    thing on a Windows host.
echo "[*] Staging DLLs + plugins → ${DIST_DIR_REL}/"
docker run --rm \
  --user "$DOCKER_USER_ARG" \
  -v "${REPO_ROOT}:/src" \
  "$IMAGE_TAG" \
  bash -c '
    set -e
    SRC=/src/'"${BUILD_DIR_REL}"'/kartend.exe
    DEST=/src/'"${DIST_DIR_REL}"'
    QT_SYSROOT=/usr/x86_64-w64-mingw32/sys-root/mingw
    QT_BIN="$QT_SYSROOT/bin"
    QT_PLUGINS="$QT_SYSROOT/lib/qt6/plugins"

    rm -rf "$DEST"
    mkdir -p "$DEST"
    cp "$SRC" "$DEST/"
    cp /src/LICENSE "$DEST/"
    cp /src/readme.md "$DEST/"

    copy_dlls() {
      local exe="$1"
      local dlls
      dlls=$(x86_64-w64-mingw32-objdump -p "$exe" 2>/dev/null \
              | awk "/DLL Name:/ {print \$3}")
      for dll in $dlls; do
        local src_dll="$QT_BIN/$dll"
        if [ -f "$src_dll" ] && [ ! -f "$DEST/$dll" ]; then
          cp "$src_dll" "$DEST/"
          copy_dlls "$src_dll"
        fi
      done
    }
    copy_dlls "$DEST/kartend.exe"

    for sub in platforms sqldrivers imageformats multimedia styles tls iconengines networkinformation; do
      if [ -d "$QT_PLUGINS/$sub" ]; then
        mkdir -p "$DEST/$sub"
        cp "$QT_PLUGINS/$sub/"*.dll "$DEST/$sub/" 2>/dev/null || true
      fi
    done
    for plugin in "$DEST"/*/*.dll; do
      [ -f "$plugin" ] && copy_dlls "$plugin"
    done

    cd /src
    rm -f '"${ZIP_NAME}"'
    ( cd "$DEST" && zip -X -r -q "/src/'"${ZIP_NAME}"'" . )
  '

ZIP_PATH="${REPO_ROOT}/${ZIP_NAME}"
ZIP_SIZE_HUMAN="$(du -h "${ZIP_PATH}" | awk '{print $1}')"
echo ""
echo "[✓] ${ZIP_NAME} (${ZIP_SIZE_HUMAN})"
echo "    Smoke-test under Wine:"
echo "      WINEPREFIX=\$HOME/.wine-kartend-test \\"
echo "      KARTEND_SMOKE_TEST_EXIT_MS=2000 \\"
echo "      wine ${DIST_DIR_REL}/kartend.exe"
