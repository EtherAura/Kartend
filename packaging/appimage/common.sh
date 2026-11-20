#!/usr/bin/env bash
# Pins and helpers shared by the AppImage scripts in this directory.
# Sourced by every one of them; not run.
#
# Every version that decides what ends up inside the AppImage is named here,
# once. To move the AppImage to another base, CMake, Qt or tool release,
# change it here and nowhere else: build-in-container.sh starts the image
# named here, and build-appimage.sh refuses to make a release build on any
# other system.
# shellcheck shell=bash
# shellcheck disable=SC2034  # the variables are read by the scripts that source this file

# --- The release base ---------------------------------------------------------
# The AppImage is built inside this container image. Every system library it
# bundles is copied from there, and the C and C++ runtime it expects from the
# user's system must be at least as new as this image's. So the image decides
# the oldest distribution the AppImage starts on: build on the oldest base
# you want to support.
#
# ubuntu:22.04 is the oldest Ubuntu LTS still supported, and its g++-12 is the
# project's compiler floor (docs/dev/building.md). The hosted `ubuntu-22.04`
# GitHub runner is on its way out (unsupported from 2027-04-17), which is why
# this is a container image and not a `runs-on:` label.
KARTEND_APPIMAGE_BASE_IMAGE="ubuntu:22.04"
KARTEND_APPIMAGE_BASE_ID="ubuntu"
KARTEND_APPIMAGE_BASE_VERSION_ID="22.04"
KARTEND_APPIMAGE_BASE_CC="gcc-12"
KARTEND_APPIMAGE_BASE_CXX="g++-12"
# The newest symbol versions that base provides. build-appimage.sh reads the
# versions every bundled file asks for and refuses a result that needs more:
# that would be an AppImage that does not start on the base it claims.
KARTEND_APPIMAGE_MAX_GLIBC="2.35"
KARTEND_APPIMAGE_MAX_GLIBCXX="3.4.30"
KARTEND_APPIMAGE_MAX_CXXABI="1.3.13"

# --- CMake --------------------------------------------------------------------
# Kitware's own build, not the base's package: 22.04 has CMake 3.22, which
# cannot generate this project's precompiled-header rules (ninja stops with
# "cmake_pch.hxx.gch ... missing and no known rule to make it"). 3.28.3 is the
# version of CI's ubuntu-24.04 jobs, so it is the one every pull request has
# already configured this tree with.
KARTEND_APPIMAGE_CMAKE_VERSION="3.28.3"
KARTEND_APPIMAGE_CMAKE_URL="https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3-linux-x86_64.tar.gz"
KARTEND_APPIMAGE_CMAKE_SHA256="804d231460ab3c8b556a42d2660af4ac7a0e21c98a7f8ee3318a74b4a9a187a6"

# --- Qt -----------------------------------------------------------------------
# The Qt Company's own Linux build, fetched with aqtinstall. The distribution's
# Qt is not used: 22.04 has Qt 6.2 (Kartend needs 6.4), and the official build
# brings the FFmpeg media backend with its own FFmpeg, so video and audio work
# without GStreamer on the user's system. 6.8 is the version CI's
# `linux-qt-newer` job runs the whole test suite on.
KARTEND_APPIMAGE_QT_VERSION="6.8.3"
KARTEND_APPIMAGE_QT_ARCH="linux_gcc_64"
KARTEND_APPIMAGE_QT_ARCH_DIR="gcc_64"
KARTEND_APPIMAGE_QT_MODULES=(qtmultimedia qtimageformats)
KARTEND_APPIMAGE_AQT_VERSION="3.3.0"

# --- QtKeychain ---------------------------------------------------------------
# Built from source against the Qt above (the distribution's package is linked
# to the distribution's Qt) and linked into kartend statically. Same release
# as the Windows build's FetchContent pin in cmake/OptionalBackends.cmake; the
# commit is checked after the clone, so a moved tag fails the build.
KARTEND_APPIMAGE_QTKEYCHAIN_REPO="https://github.com/frankosterfeld/qtkeychain.git"
KARTEND_APPIMAGE_QTKEYCHAIN_TAG="0.15.0"
KARTEND_APPIMAGE_QTKEYCHAIN_COMMIT="ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7"

# --- libarchive ---------------------------------------------------------------
# Build on the AppImage base to retain the existing glibc compatibility floor.
KARTEND_APPIMAGE_LIBARCHIVE_VERSION="3.8.11"
KARTEND_APPIMAGE_LIBARCHIVE_URL="https://github.com/libarchive/libarchive/releases/download/v$KARTEND_APPIMAGE_LIBARCHIVE_VERSION/libarchive-$KARTEND_APPIMAGE_LIBARCHIVE_VERSION.tar.xz"
KARTEND_APPIMAGE_LIBARCHIVE_SHA256="a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2"

# --- Packaging tools ----------------------------------------------------------
# Tagged upstream releases, never `continuous` (that tag is rebuilt in place,
# so a checksum of it holds only until the next upstream push). Each download
# is checked against its SHA-256 before it is used. To move to a newer
# release: change the URL, download the file, and put `sha256sum`'s answer here.
KARTEND_APPIMAGE_LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage"
KARTEND_APPIMAGE_LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
KARTEND_APPIMAGE_APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
KARTEND_APPIMAGE_APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
# The runtime is the small program at the front of every AppImage. This one is
# static and needs no FUSE library on the user's system (the old runtime
# needed libfuse2, which current distributions no longer install).
KARTEND_APPIMAGE_RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64"
KARTEND_APPIMAGE_RUNTIME_SHA256="2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d"

# --- Where prepare-build-env.sh puts CMake, Qt and QtKeychain -----------------
KARTEND_APPIMAGE_DEFAULT_PREFIX="/opt/kartend-appimage"

# --- Packages of the release base ---------------------------------------------
# Installed by prepare-build-env.sh with apt-get. Three groups:
#   1. the toolchain and the tools the scripts call;
#   2. what Kartend links: the optional backends the .deb enables too (zstd,
#      libarchive, SDL2 gamepad) and libsecret for QtKeychain;
#   3. the system libraries the official Qt build and its plugins load. They
#      have to be installed for linuxdeploy to find and bundle them.
KARTEND_APPIMAGE_APT_PACKAGES=(
  # 1. toolchain and tools (CMake is not here: see the CMake pin above)
  g++-12 ninja-build pkg-config git ca-certificates curl file binutils xz-utils
  python3 python3-venv desktop-file-utils
  # 2. Kartend's own dependencies
  libzstd-dev libsdl2-dev libsecret-1-dev libssl-dev libbz2-dev liblzma-dev
  zlib1g-dev liblz4-dev liblzo2-dev libxml2-dev libacl1-dev libattr1-dev
  # 3. what Qt and its plugins need
  libgl1-mesa-dev libegl1 libpulse-dev libxkbcommon-dev libxkbcommon-x11-0
  libfontconfig1 libfreetype6 libdbus-1-3 libglib2.0-0 libgssapi-krb5-2
  libx11-6 libx11-xcb1 libxext6 libxrandr2 libxcb1 libxcb-cursor0
  libxcb-glx0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0
  libxcb-render0 libxcb-render-util0 libxcb-shape0 libxcb-shm0 libxcb-sync1
  libxcb-xfixes0 libxcb-xkb1 libxcb-xinerama0
  libwayland-client0 libwayland-cursor0 libwayland-egl1
  libbz2-1.0 zlib1g
)

# --- What the AppImage expects from the user's system ---------------------------
# Libraries the AppImage does NOT carry: the C and C++ runtime, the graphics
# and font stack, X11's and Wayland's core and a few low-level ones. This is
# linuxdeploy's exclude list as far as Kartend's dependencies touch it.
# Bundling them does more harm than good: a bundled libGL cannot talk to the
# user's graphics driver, and a bundled C library older than the system's
# breaks the system libraries loaded next to it. Every desktop install has
# them.
#
# The list is exact: it is what the files in a release build name as needed
# and do not find in the bundle. build-appimage.sh compares every build
# against it, so a dependency that starts to leak to the host (or a
# linuxdeploy that changes what it leaves out) fails the build and is looked
# at, not shipped. smoke-test-in-container.sh installs exactly these into the
# otherwise bare container images it tests in.
KARTEND_APPIMAGE_HOST_LIBRARIES=(
  ld-linux-x86-64.so.2
  libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libresolv.so.2
  libstdc++.so.6 libgcc_s.so.1
  libGL.so.1 libEGL.so.1 libGLX.so.0 libOpenGL.so.0
  libdrm.so.2 libgbm.so.1
  libX11.so.6 libX11-xcb.so.1 libxcb.so.1 libwayland-client.so.0
  libfontconfig.so.1 libfreetype.so.6
  libasound.so.2
  libz.so.1 libcom_err.so.2 libgpg-error.so.0
)

# --- Helpers --------------------------------------------------------------------

# kartend_appimage_die MESSAGE...: print to stderr and stop.
kartend_appimage_die() {
  printf '%s: error: %s\n' "${KARTEND_APPIMAGE_PROG:-appimage}" "$*" >&2
  exit 1
}

# kartend_appimage_note MESSAGE...: one progress line on stderr.
kartend_appimage_note() {
  printf '%s: %s\n' "${KARTEND_APPIMAGE_PROG:-appimage}" "$*" >&2
}

# kartend_appimage_on_base: true when this system is the release base.
kartend_appimage_on_base() {
  [ -r /etc/os-release ] || return 1
  (
    # shellcheck disable=SC1091
    . /etc/os-release
    [ "${ID:-}" = "$KARTEND_APPIMAGE_BASE_ID" ] && [ "${VERSION_ID:-}" = "$KARTEND_APPIMAGE_BASE_VERSION_ID" ]
  )
}

# kartend_appimage_fetch URL SHA256 PATH: download URL to PATH unless a file
# with that SHA-256 is already there. A download with any other checksum is
# deleted and stops the script; nothing unverified is left on disk.
kartend_appimage_fetch() {
  local url=$1 want=$2 path=$3 got=""
  if [ -f "$path" ]; then
    got="$(sha256sum "$path" | awk '{print $1}')"
  fi
  [ "$got" = "$want" ] && return 0
  kartend_appimage_note "downloading ${url##*/}"
  rm -f "$path" "$path.download"
  curl --fail --silent --show-error --location --retry 3 --retry-delay 5 --max-time 600 \
    --output "$path.download" "$url" || kartend_appimage_die "could not download $url"
  got="$(sha256sum "$path.download" | awk '{print $1}')"
  if [ "$got" != "$want" ]; then
    rm -f "$path.download"
    kartend_appimage_die "${url##*/} has SHA-256 $got, expected $want. The pinned release changed or the download was tampered with; nothing was run."
  fi
  mv "$path.download" "$path"
}

# kartend_appimage_cmake_dir PREFIX: where prepare-build-env.sh puts CMake.
kartend_appimage_cmake_dir() {
  printf '%s/cmake/%s\n' "$1" "$KARTEND_APPIMAGE_CMAKE_VERSION"
}

# kartend_appimage_version REPO_ROOT: the Kartend version, from the VERSION file.
kartend_appimage_version() {
  tr -d '[:space:]' <"$1/VERSION"
}
