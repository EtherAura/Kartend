#!/usr/bin/env bash
# Run smoke-test.sh on a Kartend AppImage inside a clean container of some
# distribution, to see that the AppImage starts there.
#
#   packaging/appimage/smoke-test-in-container.sh ubuntu:24.04 build/appimage/Kartend-<version>-x86_64.AppImage
#   packaging/appimage/smoke-test-in-container.sh --x11 --wayland fedora:44 build/appimage/Kartend-<version>-x86_64.AppImage
#
# A container image is far barer than a desktop install: it has no graphics,
# font or X11 libraries at all. The AppImage does not carry those on purpose
# (KARTEND_APPIMAGE_HOST_LIBRARIES in common.sh), so this script first
# installs exactly that list from the distribution's own packages, and
# nothing else of the AppImage's needs: no Qt, no SDL, no FFmpeg. Whatever
# the AppImage does not bring itself and is not on the list shows up as a
# failure here. Containers have no FUSE, so the AppImage unpacks itself to run.
#
# Works with images that use apt (Debian, Ubuntu) or dnf (Fedora). Needs
# Docker, or Podman with `--docker podman`.
set -euo pipefail

KARTEND_APPIMAGE_PROG="smoke-test-in-container"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
# shellcheck source=packaging/appimage/common.sh
. "$HERE/common.sh"

usage() {
  cat <<'EOF'
Usage: smoke-test-in-container.sh [options] IMAGE APPIMAGE

  --x11            also start it under Xvfb in the container
  --wayland        also start it under Weston's headless backend in the container
  --docker PROG    container program (default: $KARTEND_APPIMAGE_DOCKER, else docker)
  -h, --help       show this help

If a file named kartend-appimage-probe lies next to the AppImage (as
build-appimage.sh leaves it), it is run inside the AppImage as well.
EOF
}

DOCKER="${KARTEND_APPIMAGE_DOCKER:-docker}"
smoke_args=()
WANT_X11=0
WANT_WAYLAND=0
IMAGE=""
APPIMAGE=""

while (($# > 0)); do
  case "$1" in
    --x11)
      WANT_X11=1
      smoke_args+=(--x11)
      shift
      ;;
    --wayland)
      WANT_WAYLAND=1
      smoke_args+=(--wayland)
      shift
      ;;
    --docker)
      (($# >= 2)) || kartend_appimage_die "--docker needs a value"
      DOCKER=$2
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    -*)
      usage >&2
      kartend_appimage_die "unknown option: $1"
      ;;
    *)
      if [ -z "$IMAGE" ]; then
        IMAGE=$1
      elif [ -z "$APPIMAGE" ]; then
        APPIMAGE=$1
      else
        kartend_appimage_die "too many arguments"
      fi
      shift
      ;;
  esac
done

if [ -z "$IMAGE" ] || [ -z "$APPIMAGE" ]; then
  usage >&2
  kartend_appimage_die "an image and an AppImage are needed"
fi
[ -f "$APPIMAGE" ] || kartend_appimage_die "$APPIMAGE does not exist"
command -v "$DOCKER" >/dev/null 2>&1 || kartend_appimage_die "'$DOCKER' is not installed"
APPIMAGE="$(readlink -f "$APPIMAGE")"
ARTIFACT_DIR="$(dirname "$APPIMAGE")"
ARTIFACT_NAME="$(basename "$APPIMAGE")"
smoke_args+=(--expect-version "$(kartend_appimage_version "$REPO_ROOT")")
if [ -f "$ARTIFACT_DIR/kartend-appimage-probe" ]; then
  smoke_args+=(--probe /artifact/kartend-appimage-probe)
fi

# Debian and Ubuntu package for each library the AppImage expects from the
# system. apt cannot be asked for a library by its file name the way dnf can,
# so the names are spelled out. A library on the list in common.sh that has
# no line here stops the script: add the package that ships it.
apt_package_for() {
  case "$1" in
    ld-linux-x86-64.so.2 | libc.so.6 | libm.so.6 | libdl.so.2 | libpthread.so.0 | librt.so.1 | \
      libresolv.so.2) echo libc6 ;;
    libstdc++.so.6) echo libstdc++6 ;;
    libgcc_s.so.1) echo libgcc-s1 ;;
    libGL.so.1) echo libgl1 ;;
    libEGL.so.1) echo libegl1 ;;
    libGLX.so.0) echo libglx0 ;;
    libOpenGL.so.0) echo libopengl0 ;;
    libdrm.so.2) echo libdrm2 ;;
    libgbm.so.1) echo libgbm1 ;;
    libX11.so.6) echo libx11-6 ;;
    libX11-xcb.so.1) echo libx11-xcb1 ;;
    libxcb.so.1) echo libxcb1 ;;
    libwayland-client.so.0) echo libwayland-client0 ;;
    libfontconfig.so.1) echo libfontconfig1 ;;
    libfreetype.so.6) echo libfreetype6 ;;
    libz.so.1) echo zlib1g ;;
    libcom_err.so.2) echo libcom-err2 ;;
    libgpg-error.so.0) echo libgpg-error0 ;;
    # renamed in Ubuntu 24.04 and Debian 13; the container picks the one it has
    libasound.so.2) echo "libasound2t64|libasound2" ;;
    *) return 1 ;;
  esac
}

apt_packages=()
dnf_packages=()
for soname in "${KARTEND_APPIMAGE_HOST_LIBRARIES[@]}"; do
  package="$(apt_package_for "$soname")" ||
    kartend_appimage_die "no Debian package known for $soname; add it to apt_package_for in this script"
  case " ${apt_packages[*]:-} " in
    *" $package "*) ;;
    *) apt_packages+=("$package") ;;
  esac
  # dnf installs whatever package provides the library file.
  dnf_packages+=("$soname()(64bit)")
done
# What smoke-test.sh itself calls, and (on request) the display servers. The
# Mesa drivers give Xvfb and Weston a software renderer.
apt_extra=()
dnf_extra=(findutils gawk grep sed coreutils)
if ((WANT_X11)); then
  apt_extra+=(xvfb xauth libgl1-mesa-dri)
  dnf_extra+=(xorg-x11-server-Xvfb xorg-x11-xauth mesa-dri-drivers)
fi
if ((WANT_WAYLAND)); then
  apt_extra+=(weston libgl1-mesa-dri)
  dnf_extra+=(weston mesa-dri-drivers)
fi

pulled=0
for attempt in 1 2 3; do
  if "$DOCKER" pull --quiet "$IMAGE" >&2; then
    pulled=1
    break
  fi
  kartend_appimage_note "pulling $IMAGE failed (attempt $attempt of 3)"
  sleep $((attempt * 10))
done
((pulled)) || kartend_appimage_die "could not pull $IMAGE"

kartend_appimage_note "testing $ARTIFACT_NAME in $IMAGE"
# shellcheck disable=SC2016  # the quoted script is expanded inside the container, not here
"$DOCKER" run --rm \
  --volume "$HERE:/kartend-appimage:ro" \
  --volume "$ARTIFACT_DIR:/artifact:ro" \
  --env "APT_PACKAGES=${apt_packages[*]} ${apt_extra[*]:-}" \
  --env "DNF_PACKAGES=${dnf_packages[*]} ${dnf_extra[*]}" \
  --env "ARTIFACT_NAME=$ARTIFACT_NAME" \
  "$IMAGE" \
  bash -c '
    set -euo pipefail
    . /etc/os-release
    echo "== ${PRETTY_NAME:-$ID}, glibc $(ldd --version | sed -n "1s/.* //p")" >&2
    if command -v apt-get >/dev/null 2>&1; then
      export DEBIAN_FRONTEND=noninteractive
      opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=30 -o Acquire::https::Timeout=30)
      timeout 300 apt-get "${opts[@]}" update -qq || { sleep 5; timeout 300 apt-get "${opts[@]}" update -qq; }
      packages=()
      for entry in $APT_PACKAGES; do
        # "a|b": the first of the alternatives this release has
        chosen=""
        for candidate in ${entry//|/ }; do
          if apt-cache show "$candidate" >/dev/null 2>&1; then
            chosen=$candidate
            break
          fi
        done
        [ -n "$chosen" ] || { echo "none of ${entry} is available in this image" >&2; exit 1; }
        packages+=("$chosen")
      done
      timeout 600 apt-get "${opts[@]}" install -y --no-install-recommends "${packages[@]}" >/dev/null
    elif command -v dnf >/dev/null 2>&1; then
      # The library names carry parentheses; keep the list out of word splitting accidents.
      read -r -a packages <<<"$DNF_PACKAGES"
      timeout 600 dnf install -y --quiet --setopt=install_weak_deps=False --setopt=retries=5 "${packages[@]}" >/dev/null
    else
      echo "this image has neither apt-get nor dnf" >&2
      exit 1
    fi
    exec /kartend-appimage/smoke-test.sh "$@" "/artifact/$ARTIFACT_NAME"
  ' bash "${smoke_args[@]}"
