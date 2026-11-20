#!/usr/bin/env bash
# Get a system ready to build the Kartend AppImage.
#
# Meant to run as root inside the release base (the container image named in
# common.sh, ubuntu:22.04), where it does four things:
#
#   1. installs the base's packages (compiler, tools, the libraries Kartend
#      and Qt link) with apt-get;
#   2. installs the pinned CMake release;
#   3. installs the Qt Company's Qt build with aqtinstall;
#   4. builds QtKeychain from its pinned commit against that Qt.
#
# CMake, Qt and QtKeychain go under --prefix (default /opt/kartend-appimage),
# which is where build-appimage.sh looks for them. Steps 2 to 4 are skipped
# when their result is already there, so a restored cache of the prefix is
# reused.
#
# build-in-container.sh runs this and then build-appimage.sh in that image;
# that is the command to use.
#
# On any other system, pass --no-system-packages and install the build
# dependencies yourself; build-appimage.sh then needs --allow-unsupported-base
# and its result is for testing only.
set -euo pipefail

KARTEND_APPIMAGE_PROG="prepare-build-env"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=packaging/appimage/common.sh
. "$HERE/common.sh"

usage() {
  cat <<EOF
Usage: prepare-build-env.sh [options]

  --prefix DIR            where CMake, Qt and QtKeychain are installed
                          (default: \$KARTEND_APPIMAGE_PREFIX, else $KARTEND_APPIMAGE_DEFAULT_PREFIX)
  --no-system-packages    skip apt-get; for systems other than $KARTEND_APPIMAGE_BASE_IMAGE
  --jobs N                parallel jobs for the QtKeychain build (default: all cores)
  -h, --help              show this help
EOF
}

PREFIX="${KARTEND_APPIMAGE_PREFIX:-$KARTEND_APPIMAGE_DEFAULT_PREFIX}"
SYSTEM_PACKAGES=1
JOBS="$(nproc)"

while (($# > 0)); do
  case "$1" in
    --prefix)
      (($# >= 2)) || kartend_appimage_die "--prefix needs a value"
      PREFIX=$2
      shift 2
      ;;
    --jobs)
      (($# >= 2)) || kartend_appimage_die "--jobs needs a value"
      JOBS=$2
      shift 2
      ;;
    --no-system-packages)
      SYSTEM_PACKAGES=0
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      kartend_appimage_die "unknown argument: $1"
      ;;
  esac
done

[ "$(uname -m)" = "x86_64" ] || kartend_appimage_die "the AppImage is x86_64 only; this machine is $(uname -m)"

# --- 1. packages of the release base -------------------------------------------
if ((SYSTEM_PACKAGES)); then
  kartend_appimage_on_base ||
    kartend_appimage_die "this is not $KARTEND_APPIMAGE_BASE_IMAGE; run inside that image, or pass --no-system-packages"
  [ "$(id -u)" -eq 0 ] || kartend_appimage_die "apt-get needs root; run as root inside the container"

  kartend_appimage_note "installing ${#KARTEND_APPIMAGE_APT_PACKAGES[@]} packages with apt-get"
  export DEBIAN_FRONTEND=noninteractive
  # A mirror that stalls is given up on after 30 s and retried, and each
  # apt-get run has a wall-clock bound, so a sick mirror ends in a labelled
  # error here and not in a job that hangs until its timeout.
  apt_opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
  timeout 300 apt-get "${apt_opts[@]}" update -qq ||
    {
      sleep 5
      timeout 300 apt-get "${apt_opts[@]}" update -qq
    } ||
    kartend_appimage_die "apt-get update failed twice"
  timeout 900 apt-get "${apt_opts[@]}" install -y --no-install-recommends "${KARTEND_APPIMAGE_APT_PACKAGES[@]}" ||
    kartend_appimage_die "apt-get install failed or ran past 15 minutes"
fi

for tool in ninja git python3 curl tar sha256sum; do
  command -v "$tool" >/dev/null 2>&1 || kartend_appimage_die "'$tool' is not installed"
done

CC_BIN="${CC:-}"
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ] && command -v "$KARTEND_APPIMAGE_BASE_CXX" >/dev/null 2>&1; then
  CC_BIN=$KARTEND_APPIMAGE_BASE_CC
  CXX_BIN=$KARTEND_APPIMAGE_BASE_CXX
fi

mkdir -p "$PREFIX"
PREFIX="$(cd "$PREFIX" && pwd)"

# --- 2. CMake -------------------------------------------------------------------
CMAKE_DIR="$(kartend_appimage_cmake_dir "$PREFIX")"
if [ -x "$CMAKE_DIR/bin/cmake" ]; then
  kartend_appimage_note "CMake $KARTEND_APPIMAGE_CMAKE_VERSION is already in $CMAKE_DIR"
else
  kartend_appimage_note "installing CMake $KARTEND_APPIMAGE_CMAKE_VERSION into $CMAKE_DIR"
  archive="$PREFIX/cmake-$KARTEND_APPIMAGE_CMAKE_VERSION.tar.gz"
  kartend_appimage_fetch "$KARTEND_APPIMAGE_CMAKE_URL" "$KARTEND_APPIMAGE_CMAKE_SHA256" "$archive"
  rm -rf "$CMAKE_DIR"
  mkdir -p "$CMAKE_DIR"
  tar -xzf "$archive" -C "$CMAKE_DIR" --strip-components=1
  rm -f "$archive"
  [ -x "$CMAKE_DIR/bin/cmake" ] || kartend_appimage_die "the CMake archive did not unpack to $CMAKE_DIR/bin/cmake"
fi
export PATH="$CMAKE_DIR/bin:$PATH"

# --- 3. Qt ----------------------------------------------------------------------
QT_DIR="$PREFIX/qt/$KARTEND_APPIMAGE_QT_VERSION/$KARTEND_APPIMAGE_QT_ARCH_DIR"
if [ -x "$QT_DIR/bin/qmake" ]; then
  kartend_appimage_note "Qt $KARTEND_APPIMAGE_QT_VERSION is already in $QT_DIR"
else
  kartend_appimage_note "installing Qt $KARTEND_APPIMAGE_QT_VERSION into $PREFIX/qt (aqtinstall $KARTEND_APPIMAGE_AQT_VERSION)"
  VENV="$PREFIX/aqt-venv"
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install --quiet --disable-pip-version-check "aqtinstall==$KARTEND_APPIMAGE_AQT_VERSION"
  # aqtinstall checks every archive against the checksum download.qt.io
  # publishes for it. A mirror can be slow or briefly unreachable, so one
  # retry from scratch.
  aqt_install() {
    rm -rf "$PREFIX/qt"
    "$VENV/bin/aqt" install-qt linux desktop "$KARTEND_APPIMAGE_QT_VERSION" "$KARTEND_APPIMAGE_QT_ARCH" \
      --modules "${KARTEND_APPIMAGE_QT_MODULES[@]}" --outputdir "$PREFIX/qt"
  }
  aqt_install || {
    kartend_appimage_note "the Qt download failed; trying once more in 20 s"
    sleep 20
    aqt_install
  } || kartend_appimage_die "could not install Qt $KARTEND_APPIMAGE_QT_VERSION"
  [ -x "$QT_DIR/bin/qmake" ] || kartend_appimage_die "aqtinstall finished but $QT_DIR/bin/qmake is missing"
  # The virtualenv was only needed for the download.
  rm -rf "$VENV"
fi
QT_REPORTED="$("$QT_DIR/bin/qmake" -query QT_VERSION)"
[ "$QT_REPORTED" = "$KARTEND_APPIMAGE_QT_VERSION" ] ||
  kartend_appimage_die "$QT_DIR is Qt $QT_REPORTED, expected $KARTEND_APPIMAGE_QT_VERSION"

# --- 4. QtKeychain ----------------------------------------------------------------
KEYCHAIN_DIR="$PREFIX/qtkeychain/$KARTEND_APPIMAGE_QTKEYCHAIN_TAG-qt$KARTEND_APPIMAGE_QT_VERSION"
if [ -f "$KEYCHAIN_DIR/lib/cmake/Qt6Keychain/Qt6KeychainConfig.cmake" ]; then
  kartend_appimage_note "QtKeychain $KARTEND_APPIMAGE_QTKEYCHAIN_TAG is already in $KEYCHAIN_DIR"
else
  kartend_appimage_note "building QtKeychain $KARTEND_APPIMAGE_QTKEYCHAIN_TAG into $KEYCHAIN_DIR"
  WORK="$(mktemp -d "$PREFIX/qtkeychain-build.XXXXXX")"
  trap 'rm -rf "$WORK"' EXIT
  git -c advice.detachedHead=false clone --quiet --depth 1 --branch "$KARTEND_APPIMAGE_QTKEYCHAIN_TAG" \
    "$KARTEND_APPIMAGE_QTKEYCHAIN_REPO" "$WORK/src"
  GOT_COMMIT="$(git -C "$WORK/src" rev-parse HEAD)"
  [ "$GOT_COMMIT" = "$KARTEND_APPIMAGE_QTKEYCHAIN_COMMIT" ] ||
    kartend_appimage_die "QtKeychain tag $KARTEND_APPIMAGE_QTKEYCHAIN_TAG is at $GOT_COMMIT, expected $KARTEND_APPIMAGE_QTKEYCHAIN_COMMIT"
  # Static and position-independent: it is linked into the kartend executable,
  # so the AppImage carries no separate QtKeychain library. libsecret (GNOME
  # Keyring and compatible services) is found through pkg-config; KWallet is
  # reached over D-Bus and needs nothing at build time.
  cmake -S "$WORK/src" -B "$WORK/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$KEYCHAIN_DIR" \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_PREFIX_PATH="$QT_DIR" \
    ${CC_BIN:+-DCMAKE_C_COMPILER="$CC_BIN"} \
    ${CXX_BIN:+-DCMAKE_CXX_COMPILER="$CXX_BIN"} \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_WITH_QT6=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TEST_APPLICATION=OFF \
    -DBUILD_TRANSLATIONS=OFF \
    -DLIBSECRET_SUPPORT=ON >"$WORK/configure.log" 2>&1 || {
    cat "$WORK/configure.log" >&2
    kartend_appimage_die "QtKeychain did not configure"
  }
  cmake --build "$WORK/build" --parallel "$JOBS" >"$WORK/build.log" 2>&1 || {
    tail -n 60 "$WORK/build.log" >&2
    kartend_appimage_die "QtKeychain did not build"
  }
  rm -rf "$KEYCHAIN_DIR"
  cmake --install "$WORK/build" >/dev/null
  [ -f "$KEYCHAIN_DIR/lib/cmake/Qt6Keychain/Qt6KeychainConfig.cmake" ] ||
    kartend_appimage_die "QtKeychain installed but its CMake package is missing from $KEYCHAIN_DIR"
  rm -rf "$WORK"
  trap - EXIT
fi

LIBARCHIVE_DIR="$PREFIX/libarchive/$KARTEND_APPIMAGE_LIBARCHIVE_VERSION"
if [ ! -f "$LIBARCHIVE_DIR/lib/libarchive.so.13" ]; then
  WORK="$(mktemp -d "$PREFIX/libarchive-build.XXXXXX")"
  trap 'rm -rf "$WORK"' EXIT
  archive="$WORK/libarchive.tar.xz"
  kartend_appimage_fetch "$KARTEND_APPIMAGE_LIBARCHIVE_URL" "$KARTEND_APPIMAGE_LIBARCHIVE_SHA256" "$archive"
  mkdir "$WORK/src"
  tar -xJf "$archive" -C "$WORK/src" --strip-components=1
  cmake -S "$WORK/src" -B "$WORK/build" -G Ninja \
    -DCMAKE_C_COMPILER="${CC_BIN:-cc}" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$LIBARCHIVE_DIR" -DCMAKE_INSTALL_LIBDIR=lib \
    -DENABLE_TEST=OFF -DENABLE_TAR=OFF -DENABLE_CPIO=OFF -DENABLE_CAT=OFF -DENABLE_UNZIP=OFF
  cmake --build "$WORK/build" --parallel "$JOBS"
  cmake --install "$WORK/build"
  install -D -m 0644 "$WORK/src/COPYING" "$LIBARCHIVE_DIR/share/licenses/libarchive/COPYING"
  rm -rf "$WORK"
  trap - EXIT
fi

kartend_appimage_note "ready: CMake in $CMAKE_DIR, Qt in $QT_DIR, QtKeychain in $KEYCHAIN_DIR"
